/*
 *  extract.ts
 *
 *  This file is part of:
 *                                GodotJS-Ext
 *              https://github.com/Daylily-Zeleen/GodotJS-Ext
 *
 *  Copyright (c) 2026-present 忘忧の (Daylily-Zeleen)
 *                 - Contact: daylily-zeleen@foxmail.com
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, see <https://www.gnu.org/licenses/>.
 */

/**
 * 函数与信号签名清单提取器（编辑器侧，Node 运行）。
 *
 * 产出：与编译产物同目录同名的 `.sig` sidecar（`foo.ts` → `<outDir>/foo.sig`）。
 * 每个源脚本一个文件，供运行时**懒加载**读取；没有清单时运行时退回函数源文本扫描。
 *
 * 三条设计约束（均来自本轮实测，详见 `.trellis/tasks/09-26-method-signal-signature/design.md`）：
 *
 *  1. **不使用 type checker**。类型文本直接取自 `.ts` 源码 AST 的 `getText()`（实测：`.js`
 *     产物里类型全被擦除，而 AST 侧 `p.type.getText()` 给出作者原文，包括引擎别名
 *     `int32` / `StringName`）。checker 反而会把别名抹成 `number` / `string`。
 *  2. **不做 Godot 类型判定**。提取器只输出**类型名字符串**，`PropertyInfo` 的映射
 *     （别名 → `Variant::Type`、`ClassDB::class_exists`、原始类名反查）全部留在 C++ 一侧，
 *     避免 JS / C++ 两份类型表漂移。
 *  3. **只输出可静态判定的信息**。默认值表达式不求值（TS 表达式无法求值），只记录
 *     "该参数可选"这一事实；剩余参数**不进**参数表，只置 vararg 标志。
 */

import * as ts from "typescript";
import * as fs from "node:fs";
import * as path from "node:path";
import * as crypto from "node:crypto";

/** 清单魔数。 */
const MAGIC = "JSIG";
/** 清单版本。格式变更时必须递增，运行时按版本拒绝不认识的清单。 */
const VERSION = 1;
/** sidecar 扩展名（与编译产物同名，只换扩展名）。 */
const SIG_EXT = ".sig";

/**
 * 引擎别名集（`scripts/typings/godot.generated.d.ts:115-121`）。
 *
 * 这些别名在 AST 上**保留原文**，在 checker 上会退化成 `number` / `string` ⇒ 只能在语法侧识别。
 * 命中即直接作为类型名输出，不再沿别名链继续走（它们的底层是 JS 原语，没有更多信息）。
 */
const ENGINE_ALIASES: Record<string, true> = {
	byte: true,
	int32: true,
	uint32: true,
	int64: true,
	uint64: true,
	float32: true,
	float64: true,
	StringName: true,
};

const KIND_METHOD = 0;
const KIND_SIGNAL = 1;

/** 字符串池索引 0 恒为空串，用作"未标注类型"的哨兵。 */
const NO_TYPE = 0;

interface ParamInfo {
	name: string;
	/** 空串表示未标注类型。 */
	type: string;
	/** `?` 或带默认值。 */
	optional: boolean;
}

interface SignatureInfo {
	/** 空串表示未标注（返回值未知 ⇒ 消费端按"任意值"处理，而不是 `void`）。 */
	returnType: string;
	isVararg: boolean;
	params: ParamInfo[];
}

export interface MemberInfo {
	kind: number;
	name: string;
	/** 无重载时长度为 1；有重载时按源码声明顺序存全部签名。 */
	signatures: SignatureInfo[];
}

/** `runExtraction` 的入参（与 CLI 参数同形）。 */
export interface ExtractionArgs {
	project: string;
	dump: boolean;
	verbose: boolean;
}

/** `runExtraction` 的结果。失败时 `messages` 是给用户看的诊断文本。 */
export interface ExtractionResult {
	ok: boolean;
	messages: string[];
	scanned: number;
	written: number;
	upToDate: number;
	empty: number;
	ignored: number;
	nonGodot: number;
}

// ---------------------------------------------------------------------------
// 字符串池与二进制写入
// ---------------------------------------------------------------------------

class StringPool {
	private readonly _map = new Map<string, number>([["", NO_TYPE]]);
	private readonly _list: string[] = [""];

	get(p_value: string): number {
		const hit = this._map.get(p_value);
		if (hit !== undefined) {
			return hit;
		}
		const index = this._list.length;
		this._list.push(p_value);
		this._map.set(p_value, index);
		return index;
	}

	get all(): readonly string[] {
		return this._list;
	}
}

class ByteWriter {
	private readonly _bytes: number[] = [];

	u8(p_value: number): void {
		this._bytes.push(p_value & 0xff);
	}

	raw(p_value: Uint8Array): void {
		for (const byte of p_value) {
			this._bytes.push(byte);
		}
	}

	ascii(p_value: string): void {
		for (let i = 0; i < p_value.length; ++i) {
			this._bytes.push(p_value.charCodeAt(i) & 0xff);
		}
	}

	/** LEB128 无符号整数。 */
	varuint(p_value: number): void {
		let value = p_value >>> 0;
		while (value >= 0x80) {
			this._bytes.push((value & 0x7f) | 0x80);
			value >>>= 7;
		}
		this._bytes.push(value);
	}

	toBuffer(): Buffer {
		return Buffer.from(this._bytes);
	}
}

// ---------------------------------------------------------------------------
// AST 提取
// ---------------------------------------------------------------------------

/**
 * 去除类型文本里的注释与多余空白。
 *
 * `getText()` 返回的是**源码原文**，实测会把注释与换行一起带出来
 * （`Map<string, /* inner *​/ number>`、多行泛型）。清单只用于查表匹配，
 * 不解析类型表达式，因此这里做一次轻量清洗即可（不做完整词法扫描）。
 */
function normalizeTypeText(p_text: string): string {
	return p_text
		.replace(/\/\*[\s\S]*?\*\//g, " ")
		.replace(/\/\/[^\n]*/g, " ")
		.replace(/\s+/g, " ")
		.trim();
}

/** 收集同一文件内的类型别名声明，用于别名链解析（不跨文件，也不需要 checker）。 */
function collectAliases(p_source: ts.SourceFile): Map<string, ts.TypeNode> {
	const aliases = new Map<string, ts.TypeNode>();
	for (const statement of p_source.statements) {
		if (ts.isTypeAliasDeclaration(statement)) {
			aliases.set(statement.name.text, statement.type);
		}
	}
	return aliases;
}

/**
 * 把类型节点解析成写入清单的类型名。
 *
 * 规则（实测依据见 design.md §5.1）：
 *  - 引擎别名（`int32` 等）直接返回其名；
 *  - 同文件的类型别名继续沿链走，直到命中引擎别名或落到非引用节点；
 *  - 非类型引用节点（关键字 / 联合 / 字面量 / 数组 …）返回清洗后的原文；
 *  - 跨文件别名 / 无法解析的名字原样返回，由消费端判定为不可映射。
 */
function resolveTypeName(p_node: ts.TypeNode | undefined, p_aliases: Map<string, ts.TypeNode>): string {
	const seen = new Set<string>();
	let current: ts.TypeNode | undefined = p_node;
	while (current) {
		if (!ts.isTypeReferenceNode(current)) {
			return normalizeTypeText(current.getText());
		}
		const name = current.typeName.getText();
		if (ENGINE_ALIASES[name] === true) {
			return name;
		}
		const target = p_aliases.get(name);
		if (!target || seen.has(name)) {
			return name;
		}
		seen.add(name);
		current = target;
	}
	return "";
}

function isStaticMember(p_member: ts.ClassElement): boolean {
	if (!ts.canHaveModifiers(p_member)) {
		return false;
	}
	const modifiers = ts.getModifiers(p_member);
	return !!modifiers?.some((m) => m.kind === ts.SyntaxKind.StaticKeyword);
}

/** 提取一个形参表。剩余参数**不入表**（消费端只用 vararg 标志表达它）。 */
function collectParams(p_params: readonly ts.ParameterDeclaration[], p_aliases: Map<string, ts.TypeNode>): ParamInfo[] {
	const result: ParamInfo[] = [];
	for (const param of p_params) {
		if (param.dotDotDotToken) {
			continue;
		}
		result.push({
			name: param.name.getText(),
			type: resolveTypeName(param.type, p_aliases),
			// TS 语义：带默认值的参数同样是可选参数。
			optional: !!param.questionToken || !!param.initializer,
		});
	}
	return result;
}

function hasRestParam(p_params: readonly ts.ParameterDeclaration[]): boolean {
	const last = p_params.length > 0 ? p_params[p_params.length - 1] : undefined;
	return !!last?.dotDotDotToken;
}

/**
 * 可选参数的**个数**，只统计形参表**末尾连续可选**的那一段。
 *
 * 为什么不能简单数 `optional`：TS 只对 `?` 强制"必填不能跟在可选之后"（TS1016），**带初始值的
 * 参数不受此约束** —— `f(a: number = 1, b: number)` 是合法代码，而 `b` 是必填的。把它算进
 * `default_count` 会让消费端认为最小 arity = 2-1 = 1，于是 `f(1)` 被判合法，实际 `b` 拿到
 * `undefined`。
 */
function countTrailingOptional(p_params: readonly ParamInfo[]): number {
	let count = 0;
	for (let i = p_params.length - 1; i >= 0; --i) {
		if (!p_params[i].optional) {
			break;
		}
		++count;
	}
	return count;
}

/**
 * 从一个函数类型节点（信号载荷 `Signal<(a: number) => void>` 的类型实参）提取签名。
 */
function collectFunctionTypeSignature(p_node: ts.FunctionTypeNode, p_aliases: Map<string, ts.TypeNode>): SignatureInfo {
	return {
		returnType: resolveTypeName(p_node.type, p_aliases),
		isVararg: hasRestParam(p_node.parameters),
		params: collectParams(p_node.parameters, p_aliases),
	};
}

/**
 * 信号的声明形态：`@bind.signal() accessor x!: Signal<(a: number) => void>`。
 *
 * 只按类型形状识别（`Signal<T>` 且 `T` 是函数类型），不检查装饰器名字 ——
 * 运行时的信号集合本就来自 JS 侧注册，清单只是按名补充参数类型，
 * 误识别的条目不会被任何已注册信号取用。
 */
function tryCollectSignal(p_member: ts.ClassElement, p_aliases: Map<string, ts.TypeNode>): MemberInfo | undefined {
	if (!ts.isPropertyDeclaration(p_member) || !p_member.name || !ts.isIdentifier(p_member.name)) {
		return undefined;
	}
	const typeNode = p_member.type;
	if (!typeNode || !ts.isTypeReferenceNode(typeNode) || typeNode.typeName.getText() !== "Signal") {
		return undefined;
	}
	const typeArguments = typeNode.typeArguments;
	if (!typeArguments || typeArguments.length !== 1) {
		return undefined;
	}
	const payload = typeArguments[0];
	if (!ts.isFunctionTypeNode(payload)) {
		return undefined;
	}
	return { kind: KIND_SIGNAL, name: p_member.name.text, signatures: [collectFunctionTypeSignature(payload, p_aliases)] };
}

/**
 * 收集一个类声明里的实例方法与信号。
 *
 * 方法侧刻意**不做**过滤性的语法判断（有体/无体、`abstract`、`declare` 都收）：
 * 重载集必须完整记录（TS emit 会擦除重载签名，只有源码侧看得到），
 * 且消费端是按名字查已注册方法的，多余条目不会被取用，只是少量体积。
 */
function collectClassMembers(p_class: ts.ClassDeclaration, p_aliases: Map<string, ts.TypeNode>): MemberInfo[] {
	const methodOrder: string[] = [];
	const methodGroups = new Map<string, SignatureInfo[]>();
	const signals: MemberInfo[] = [];

	for (const member of p_class.members) {
		// 静态函数不在父任务的支持范围内（Q1=(c)），运行期也不在 prototype 上。
		if (ts.isMethodDeclaration(member)) {
			if (isStaticMember(member) || !member.name || !ts.isIdentifier(member.name)) {
				continue;
			}
			const name = member.name.text;
			if (name === "constructor") {
				continue;
			}
			const signature: SignatureInfo = {
				returnType: resolveTypeName(member.type, p_aliases),
				isVararg: hasRestParam(member.parameters),
				params: collectParams(member.parameters, p_aliases),
			};
			const group = methodGroups.get(name);
			if (group) {
				group.push(signature);
			} else {
				methodGroups.set(name, [signature]);
				methodOrder.push(name);
			}
			continue;
		}

		const signal = tryCollectSignal(member, p_aliases);
		if (signal) {
			signals.push(signal);
		}
	}

	const members: MemberInfo[] = [];
	for (const name of methodOrder) {
		members.push({ kind: KIND_METHOD, name, signatures: methodGroups.get(name)! });
	}
	members.push(...signals);
	return members;
}

// ---------------------------------------------------------------------------
// 序列化
// ---------------------------------------------------------------------------

export function serialize(p_sourceMd5: string, p_members: readonly MemberInfo[]): Buffer {
	const pool = new StringPool();

	// 先把字符串池填满，再写文件（池在头部，必须一次成型）。
	const encoded: Array<{ kind: number; name: number; signatures: Array<{ flags: number; returnType: number; params: Array<{ flags: number; name: number; type: number }>; defaultCount: number }> }> = [];
	for (const member of p_members) {
		const signatures = member.signatures.map((signature) => {
			const params = signature.params.map((param) => ({
				flags: (param.optional ? 1 : 0),
				name: pool.get(param.name),
				type: param.type ? pool.get(param.type) : NO_TYPE,
			}));
			return {
				flags: (signature.returnType ? 1 : 0) | (signature.isVararg ? 2 : 0),
				returnType: signature.returnType ? pool.get(signature.returnType) : NO_TYPE,
				params,
				defaultCount: countTrailingOptional(signature.params),
			};
		});
		encoded.push({ kind: member.kind, name: pool.get(member.name), signatures });
	}

	const writer = new ByteWriter();
	writer.ascii(MAGIC);
	writer.u8(VERSION);
	writer.raw(Buffer.from(p_sourceMd5, "hex"));

	const strings = pool.all;
	writer.varuint(strings.length);
	for (let i = 0; i < strings.length; ++i) {
		const bytes = Buffer.from(strings[i], "utf8");
		writer.varuint(bytes.length);
		writer.raw(bytes);
	}

	writer.varuint(encoded.length);
	for (const member of encoded) {
		writer.u8(member.kind);
		writer.varuint(member.name);
		writer.varuint(member.signatures.length);
		for (const signature of member.signatures) {
			writer.u8(signature.flags);
			if (signature.flags & 1) {
				writer.varuint(signature.returnType);
			}
			writer.varuint(signature.params.length);
			writer.varuint(signature.defaultCount);
			for (const param of signature.params) {
				writer.u8(param.flags);
				writer.varuint(param.name);
				writer.varuint(param.type);
			}
		}
	}
	return writer.toBuffer();
}

// ---------------------------------------------------------------------------
// 增量：读取既有 sidecar 的源文件 md5
// ---------------------------------------------------------------------------

function readExistingSourceMd5(p_sigPath: string): string | undefined {
	let buffer: Buffer;
	try {
		buffer = fs.readFileSync(p_sigPath);
	} catch {
		return undefined;
	}
	if (buffer.length < 4 + 1 + 16) {
		return undefined;
	}
	if (buffer.toString("latin1", 0, 4) !== MAGIC || buffer[4] !== VERSION) {
		return undefined;
	}
	return buffer.toString("hex", 5, 21);
}

// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

function parseArgs(p_argv: readonly string[]): { project: string; dump: boolean; verbose: boolean } {
	let project = process.cwd();
	let dump = false;
	let verbose = false;
	for (let i = 0; i < p_argv.length; ++i) {
		const arg = p_argv[i];
		if (arg === "--project" && i + 1 < p_argv.length) {
			project = path.resolve(p_argv[++i]);
		} else if (arg === "--dump") {
			dump = true;
		} else if (arg === "--verbose") {
			verbose = true;
		}
	}
	return { project, dump, verbose };
}

/**
 * Godot 编辑器自身的忽略规则（照抄 `EditorFileSystem`，见下方逐条出处）。
 *
 * 之所以不在提取器里另搞一套：`.gdignore` / 嵌套 `project.godot` 是**编辑器概念**，
 * Godot 侧扫描不到它们，本提取器若自创规则就会与编辑器看到的脚本集合不一致。
 *
 * 出处（引擎 `editor/file_system/editor_file_system.cpp`）：
 *  - 任一路径段以 `.` 开头 ⇒ 忽略。`_scan_fs_changes` 有两条独立判据（`:1478` 文件走
 *    `current_is_hidden()`、`:1483` 目录走 `begins_with(".")`）⇒ **文件本身也要参与**，
 *    所以这里最后一段同样生效；
 *  - 目录内含 `.gdignore`/`project.godot` ⇒ 忽略（`:3511-3531` `_should_skip_directory`）；
 *  - `outDir`（编译产物）⇒ 忽略。TS 自身的默认 `exclude` 已含它，但显式判一次不依赖该默认值。
 *
 * @param p_relative 相对项目根、已去 `.ts` 后缀的路径（分隔符归一为 `/`）
 * @param p_cache    目录忽略标记的缓存，避免同一目录对每个文件重复 stat
 */
function shouldIgnorePath(p_relative: string, p_root: string, p_outDir: string, p_cache: Map<string, boolean>): boolean {
	const segments = p_relative.split("/");
	for (const segment of segments) {
		if (segment.startsWith(".")) {
			return true;
		}
	}
	const outRel = p_outDir.startsWith(p_root + "/") ? p_outDir.substring(p_root.length + 1) : "";
	if (outRel.length > 0 && (p_relative === outRel || p_relative.startsWith(outRel + "/"))) {
		return true;
	}
	for (let i = 0; i < segments.length - 1; ++i) {
		const dirRel = segments.slice(0, i + 1).join("/");
		let ignored = p_cache.get(dirRel);
		if (ignored === undefined) {
			const dirAbs = path.join(p_root, dirRel);
			ignored = fs.existsSync(path.join(dirAbs, ".gdignore")) || fs.existsSync(path.join(dirAbs, "project.godot"));
			p_cache.set(dirRel, ignored);
		}
		if (ignored) {
			return true;
		}
	}
	return false;
}

/** 引擎类型统一从该模块导入（`import { Node } from "godot"`），故按**来源模块**判定，不维护类名表。 */
const GODOT_MODULE = "godot";

/**
 * 单文件判据：这个文件是不是"默认导出一个 Godot 脚本"。
 *
 * 运行期判据是 `exports.default` 且 `class_obj[Symbol(ClassId)]` 为 Uint32
 * （`ScriptClassInfo::_parse_script_class`，`jsb_class_info.cpp:820-853`）——**第三条要靠 JS 求值**，
 * 静态只能近似。故判据是**三态**，且刻意**宽松**：
 *
 *  - 基类标识符从 `"godot"` 模块导入 ⇒ 是 Godot 脚本；
 *  - 无默认导出 / 默认导出不是类 / 类无 `extends` / 基类链在**本文件内**可证不是 ⇒ 不是；
 *  - 其余（跨文件继承 `extends Child`、`extends Mixin(Base)` 这类表达式、基类不可解析）⇒ **未知**，
 *    按"是"处理。理由：判错的代价是那个脚本**永远没有清单**（静默退回源文本扫描，丢掉全部
 *    参数名/类型/返回值），比多写一份没人读的清单严重得多。
 *
 * 刻意**不做**跨文件传播：那需要图遍历 + 环处理 + tsconfig `paths` 反解，收益却为零
 * （`project/` 下 18 份清单实测全是 Godot 脚本）。
 */
type GodotVerdict = "godot" | "not" | "unknown";

/** 默认导出的目标：`export default class X` 或 `export default X`。 */
function findDefaultExportedClassName(p_source: ts.SourceFile): string | undefined {
	for (const statement of p_source.statements) {
		if (ts.isClassDeclaration(statement) && statement.name) {
			const modifiers = ts.canHaveModifiers(statement) ? ts.getModifiers(statement) : undefined;
			if (!!modifiers?.some((m) => m.kind === ts.SyntaxKind.DefaultKeyword)) {
				return statement.name.text;
			}
		}
		if (ts.isExportAssignment(statement) && !statement.isExportEquals && ts.isIdentifier(statement.expression)) {
			return statement.expression.text;
		}
	}
	return undefined;
}

/** `extends` 后的标识符。`extends Mixin(Base)` 这类表达式返回 undefined（⇒ 未知）。 */
function findBaseIdentifier(p_class: ts.ClassDeclaration): string | undefined {
	const clause = p_class.heritageClauses?.find((c) => c.token === ts.SyntaxKind.ExtendsKeyword);
	const type = clause?.types[0];
	if (!type) {
		return undefined;
	}
	return ts.isIdentifier(type.expression) ? type.expression.text : undefined;
}

function classifyClassName(p_name: string, p_localClasses: Map<string, ts.ClassDeclaration>, p_importedFrom: Map<string, string>, p_seen: Set<string>): GodotVerdict {
	if (p_seen.has(p_name)) {
		return "unknown"; // 继承环
	}
	p_seen.add(p_name);
	const classDecl = p_localClasses.get(p_name);
	if (!classDecl) {
		// 不是本文件的类 ⇒ 只能看它从哪导入；无法进一步判定时按"未知"放行。
		return p_importedFrom.get(p_name) === GODOT_MODULE ? "godot" : "unknown";
	}
	const base = findBaseIdentifier(classDecl);
	if (!base) {
		return "not"; // 本文件的类且无 extends ⇒ 可证不是 Godot 脚本
	}
	return classifyClassName(base, p_localClasses, p_importedFrom, p_seen);
}

function isGodotScript(p_source: ts.SourceFile): boolean {
	const exported = findDefaultExportedClassName(p_source);
	if (exported === undefined) {
		return false;
	}
	const localClasses = new Map<string, ts.ClassDeclaration>();
	const importedFrom = new Map<string, string>();
	for (const statement of p_source.statements) {
		if (ts.isClassDeclaration(statement) && statement.name) {
			localClasses.set(statement.name.text, statement);
		}
		if (ts.isImportDeclaration(statement) && statement.importClause && ts.isStringLiteral(statement.moduleSpecifier)) {
			const moduleName = statement.moduleSpecifier.text;
			const clause = statement.importClause;
			if (clause.name) {
				importedFrom.set(clause.name.text, moduleName);
			}
			const bindings = clause.namedBindings;
			if (bindings && ts.isNamedImports(bindings)) {
				for (const element of bindings.elements) {
					importedFrom.set(element.name.text, moduleName);
				}
			}
		}
	}
	return classifyClassName(exported, localClasses, importedFrom, new Set<string>()) !== "not";
}

function main(): number {
	const args = parseArgs(process.argv.slice(2));
	const result = runExtraction(args, process.argv.slice(2).includes("--dump") || args.dump);
	for (const message of result.messages) {
		if (result.ok) {
			console.log(message);
		} else {
			console.error(message);
		}
	}
	return result.ok ? 0 : 1;
}

/**
 * 全项目签名提取（**纯函数式入口**，同时服务 CLI 与常驻工具进程）。
 *
 * 与 CLI 的差别只有一处：不调用 `process.exit`，失败时返回 `ok=false` 与诊断文本，
 * 由调用方决定怎么报告（常驻进程要把错误回给宿主，而不是自杀）。
 */
export function runExtraction(args: ExtractionArgs, pDump: boolean): ExtractionResult {
	const configPath = resolveConfigPath(args.project);
	const resolvedConfigPath = configPath.path;
	if (resolvedConfigPath === undefined) {
		return { ok: false, messages: [configPath.reason ?? "no typescript config found"], scanned: 0, written: 0, upToDate: 0, empty: 0, ignored: 0, nonGodot: 0 };
	}
	const configFile = ts.readConfigFile(resolvedConfigPath, ts.sys.readFile);
	if (configFile.error) {
		return {
			ok: false,
			messages: ["[signature] failed to read " + resolvedConfigPath + ": " + ts.flattenDiagnosticMessageText(configFile.error.messageText, " ")],
			scanned: 0, written: 0, upToDate: 0, empty: 0, ignored: 0, nonGodot: 0,
		};
	}
	const parsed = ts.parseJsonConfigFileContent(configFile.config, ts.sys, args.project);
	if (parsed.errors.length > 0) {
		return {
			ok: false,
			messages: parsed.errors.slice(0, 8).map((error) => "[signature] " + resolvedConfigPath + ": " + ts.flattenDiagnosticMessageText(error.messageText, " ")),
			scanned: 0, written: 0, upToDate: 0, empty: 0, ignored: 0, nonGodot: 0,
		};
	}

	// 编译产物目录来自配置的 outDir（与 .paths_mapping / 编译后的 .js 同目录）。
	//
	// JS 项目（jsconfig.json）不带 outDir：那时脚本**就地运行**（`res://x.js` 就是产物），
	// 所以把项目根当作产物根，sidecar 落在 `<project>/<rel>.sig` —— 与运行时
	// `signature_load()` 的推导式（`<module_id 去 .js>` + `.sig`）一致。
	const outDir = parsed.options.outDir ?? args.project;

	const normalizedRoot = args.project.replace(/\\/g, "/");
	const normalizedOutDir = outDir.replace(/\\/g, "/");
	// 目录忽略标记的缓存：同一目录下的每个文件都会查同样的祖先目录，缓存避免重复 stat。
	const ignoreDirCache = new Map<string, boolean>();
	const fileNames = enumerateSourceFiles(parsed, args.project, normalizedRoot);

	let scanned = 0;
	let written = 0;
	let skipped = 0;
	let pruned = 0;
	let ignored = 0;
	let excluded = 0;

	for (const fileName of fileNames) {
		const normalized = fileName.replace(/\\/g, "/");
		if (normalized.includes("/node_modules/")) {
			continue;
		}
		if (!normalized.startsWith(normalizedRoot + "/")) {
			continue;
		}
		const extension = sourceExtension(normalized);
		if (extension === undefined) {
			continue;
		}
		const relative = normalized.substring(normalizedRoot.length + 1, normalized.length - extension.length);
		// Godot 编辑器看不到的文件（隐藏目录/文件、`.gdignore`、嵌套项目、编译产物）一律不处理。
		if (shouldIgnorePath(relative, args.project, normalizedOutDir, ignoreDirCache)) {
			++ignored;
			continue;
		}
		++scanned;

		const sigPath = path.join(outDir, relative + SIG_EXT);
		const source = fs.readFileSync(fileName, "utf8");
		const sourceMd5 = crypto.createHash("md5").update(source, "utf8").digest("hex");

		// 增量：源未变则整脚本跳过（不解析 AST、不重写文件）。
		if (readExistingSourceMd5(sigPath) === sourceMd5) {
			++skipped;
			continue;
		}

		const sourceFile = ts.createSourceFile(fileName, source, ts.ScriptTarget.ES2022, true, scriptKindOf(extension));
		// 单文件判据：不是 Godot 脚本就不产出清单（并清掉可能存在的旧清单）。
		if (!isGodotScript(sourceFile)) {
			++excluded;
			if (pDump) {
				console.log(JSON.stringify({ module: relative + extension, md5: sourceMd5, godot: false, members: [] }));
			} else {
				try {
					fs.unlinkSync(sigPath);
				} catch {
					// 本来就不存在，忽略。
				}
			}
			continue;
		}
		const aliases = collectAliases(sourceFile);
		const members: MemberInfo[] = [];
		for (const statement of sourceFile.statements) {
			if (ts.isClassDeclaration(statement)) {
				members.push(...collectClassMembers(statement, aliases));
			}
		}

		if (pDump) {
			console.log(JSON.stringify({ module: relative + extension, md5: sourceMd5, members }));
		}

		if (members.length === 0) {
			// 没有类成员的脚本不产出清单：消费端查不到文件即走回退路径。
			++pruned;
			if (!pDump) {
				try {
					fs.unlinkSync(sigPath);
				} catch {
					// 本来就不存在，忽略。
				}
			}
			continue;
		}

		if (pDump) {
			continue;
		}

		fs.mkdirSync(path.dirname(sigPath), { recursive: true });
		fs.writeFileSync(sigPath, serialize(sourceMd5, members));
		++written;
		if (args.verbose) {
			console.log("[signature] " + relative + SIG_EXT + " (" + members.length + " members)");
		}
	}

	return {
		ok: true,
		messages: [
			"[signature] scripts=" + scanned + " written=" + written + " up-to-date=" + skipped
					+ " empty=" + pruned + " ignored=" + ignored + " non-godot=" + excluded,
		],
		scanned, written, upToDate: skipped, empty: pruned, ignored, nonGodot: excluded,
	};
}

/** 配置来源：TS 项目用 `tsconfig.json`，纯 JS 项目用 `jsconfig.json`。 */
function resolveConfigPath(p_project: string): { path?: string; reason?: string } {
	const tsconfig = path.join(p_project, "tsconfig.json");
	if (fs.existsSync(tsconfig)) {
		return { path: tsconfig };
	}
	const jsconfig = path.join(p_project, "jsconfig.json");
	if (fs.existsSync(jsconfig)) {
		return { path: jsconfig };
	}
	return { reason: "[signature] neither tsconfig.json nor jsconfig.json found in " + p_project };
}

/** 源文件后缀（含点）；不是可处理的源文件时返回 undefined。 */
function sourceExtension(p_normalized_path: string): string | undefined {
	if (p_normalized_path.endsWith(".d.ts")) {
		return undefined;
	}
	if (p_normalized_path.endsWith(".ts")) {
		return ".ts";
	}
	if (p_normalized_path.endsWith(".js")) {
		return ".js";
	}
	return undefined;
}

function scriptKindOf(p_extension: string): ts.ScriptKind {
	return p_extension === ".js" ? ts.ScriptKind.JS : ts.ScriptKind.TS;
}

/**
 * 待处理的源文件集合。
 *
 * `fileNames` 优先，但 JS 项目可能空手而归：**实测**预设 `jsconfig.json`
 * （`{"compilerOptions":{"module":"node16","target":"es2022"}}`）下 `allowJs` 未开，
 * `parseJsonConfigFileContent` 返回 `fileNames: []` —— 光放开后缀过滤是看不到任何 `.js` 的。
 * 因此这里显式打开 `allowJs`（不改写作者的项目文件，只改内存中的 options），
 * 并在 `fileNames` 仍为空时用与 `shouldIgnorePath` 同源的规则**兜底枚举**。
 */
function enumerateSourceFiles(p_parsed: ts.ParsedCommandLine, p_project: string, p_normalized_root: string): string[] {
	const collected = new Set<string>();
	if (p_parsed.options.allowJs !== true) {
		p_parsed.options.allowJs = true;
	}
	for (const fileName of p_parsed.fileNames) {
		collected.add(fileName);
	}
	for (const fileName of ts.sys.readDirectory(p_project, [".ts", ".js"], undefined, undefined)) {
		const normalized = fileName.replace(/\\/g, "/");
		if (normalized.startsWith(p_normalized_root + "/")) {
			collected.add(fileName);
		}
	}
	return Array.from(collected);
}

/** 一个待处理的 Godot 源脚本。 */
export interface GodotSourceFile {
	/** 绝对路径（供工具直接读文件）。 */
	absolute: string;
	/** `res://<rel><ext>` —— 与运行时 `ScriptClassInfo::module_id` 的推导同源。 */
	resPath: string;
	/** 源文件 md5（刷新判据）。 */
	sourceMd5: string;
}

/**
 * 列出项目里全部 **Godot 脚本**（判据与签名提取完全一致：同一套配置解析、忽略规则、
 * `isGodotScript`）。
 *
 * 帮助文档要按同一集合推送：多一个少一个都会让脚本的文档与签名来自不同的判定，
 * 所以这里**复用**提取逻辑而不是另写一套枚举。
 */
export function listGodotScripts(p_project: string): GodotSourceFile[] {
	const configPath = resolveConfigPath(p_project);
	const resolvedConfigPath = configPath.path;
	if (resolvedConfigPath === undefined) {
		return [];
	}
	const configFile = ts.readConfigFile(resolvedConfigPath, ts.sys.readFile);
	if (configFile.error) {
		return [];
	}
	const parsed = ts.parseJsonConfigFileContent(configFile.config, ts.sys, p_project);
	const normalizedRoot = p_project.replace(/\\/g, "/");
	const outDir = parsed.options.outDir ?? p_project;
	const normalizedOutDir = outDir.replace(/\\/g, "/");
	const ignoreDirCache = new Map<string, boolean>();
	const result: GodotSourceFile[] = [];

	for (const fileName of enumerateSourceFiles(parsed, p_project, normalizedRoot)) {
		const normalized = fileName.replace(/\\/g, "/");
		if (normalized.includes("/node_modules/") || !normalized.startsWith(normalizedRoot + "/")) {
			continue;
		}
		const extension = sourceExtension(normalized);
		if (extension === undefined) {
			continue;
		}
		const relative = normalized.substring(normalizedRoot.length + 1, normalized.length - extension.length);
		if (shouldIgnorePath(relative, p_project, normalizedOutDir, ignoreDirCache)) {
			continue;
		}
		const source = fs.readFileSync(fileName, "utf8");
		const sourceFile = ts.createSourceFile(fileName, source, ts.ScriptTarget.ES2022, true, scriptKindOf(extension));
		if (!isGodotScript(sourceFile)) {
			continue;
		}
		result.push({
			absolute: fileName,
			resPath: "res://" + relative + extension,
			sourceMd5: crypto.createHash("md5").update(source, "utf8").digest("hex"),
		});
	}
	return result;
}

/** CLI 入口。`require` 该模块（常驻工具进程）时**不会**触发它。 */
if (require.main === module) {
	process.exit(main());
}
