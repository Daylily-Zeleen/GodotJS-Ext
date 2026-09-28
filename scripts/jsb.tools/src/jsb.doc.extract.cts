/*
 *  extract.ts
 *
 *  This file is part of:
 *                                GodotJS-Ext
 *              https://github.com/Daylily-Zeleen/GodotJS-Ext
 *
 *  Copyright (c) 2026-present 忘忧の (Daylily-Zeleen)                  *
 *                 - Contact: daylily-zeleen@foxmail.com                *
 *
 *  This library is free software; you can redistribute it and/or       *
 *  modify it under the terms of the GNU Lesser General Public          *
 *  License as published by the Free Software Foundation; either        *
 *  version 2.1 of the License, or (at your option) any later version.  *
 *
 *  This library is distributed in the hope that it will be useful,     *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of      *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU    *
 *  Lesser General Public License for more details.                     *
 *                                                                      *
 *  You should have received a copy of the GNU Lesser General Public    *
 *  License along with this library; if not,                            *
 *  see <https://www.gnu.org/licenses/>.                                *
 */

/**
 * 源文件文档注释提取（编辑器侧，与函数/信号签名清单共用同一趟 AST）。
 *
 * 规则（用户 2026-09-28 拍板，见 `.trellis/tasks/09-28-comment-doc-annotation/design.md` §6）：
 *
 *  1. 解析对象是**源文件**（`.ts` / `.js`），不是编译产物 —— 编辑器里源文件就在磁盘上。
 *  2. 取「声明之前的**最后一段** `/** … *​/`」，其中「声明」= 其全部装饰器与修饰符 + 声明本体
 *     ⇒ 注释写在装饰器**之上**或**之下**都算。
 *  3. 该注释末端到声明起点之间出现**空行** ⇒ 判为没有文档（避免把文件里其它说明当成它的说明）。
 *  4. **首行 = brief，全文 = description**。
 *  5. 只取紧邻声明的那一段，**不看**文件头部的无关注释。
 *
 * ⚠️ 关联必须走**源码文本扫描**，不能依赖 `node.jsDoc`：**实测**（ts 6.0.3）注释写在装饰器
 * **之后**时，`node.jsDoc` 与 `ts.getJSDocCommentsAndTags` **都是空的**，只有源码里看得到。
 * 两个位置因此共用同一条实现。
 */

import * as ts from "typescript";

/** 文档只取这几种成员（与运行时 `ScriptClassInfo` 的登记面一一对应）。 */
export const DOC_KIND_METHOD = "method";
export const DOC_KIND_PROPERTY = "property";
export const DOC_KIND_SIGNAL = "signal";
export const DOC_KIND_CONSTANT = "constant";

export type DocKind = typeof DOC_KIND_METHOD | typeof DOC_KIND_PROPERTY | typeof DOC_KIND_SIGNAL | typeof DOC_KIND_CONSTANT;

/** 一个成员的文档。两个字段都可为空串（空 = 没有写）。 */
export interface MemberDoc {
	kind: DocKind;
	name: string;
	brief: string;
	description: string;
}

/** 一个脚本类的文档。 */
export interface ScriptDoc {
	class: { brief: string; description: string };
	members: MemberDoc[];
}

const EMPTY_DOC = { brief: "", description: "" };

/**
 * 去掉块注释的外壳与每行前导 `*`，得到一个 JSDoc 的正文。
 *
 * 保留正文内部的空行与 markdown（分段信息在 `splitBrief` 里用）。
 */
function unwrapComment(p_raw: string): string {
	const inner = p_raw.slice(3, p_raw.length - 2); // 去掉 "/**" 与 "*/"
	const lines = inner.split(/\r?\n/);
	const out: string[] = [];
	for (let i = 0; i < lines.length; ++i) {
		// 只剥"装饰性"的前导空白与星号；行内内容不动。
		out.push(lines[i].replace(/^[ \t]*\*?[ \t]?/, ""));
	}
	// 去掉首尾空行（注释壳自带的换行），中间的空行保留。
	while (out.length > 0 && out[0].trim() === "") out.shift();
	while (out.length > 0 && out[out.length - 1].trim() === "") out.pop();
	return out.join("\n");
}

/** 首行 = brief，全文 = description。首行为空时 brief 为空。 */
function splitBrief(p_text: string): { brief: string; description: string } {
	const newline = p_text.indexOf("\n");
	if (newline < 0) {
		const brief = p_text.trim();
		return { brief, description: brief };
	}
	return { brief: p_text.slice(0, newline).trim(), description: p_text.trim() };
}

/**
 * 取挂在 `p_node` 上的文档注释。
 *
 * span = `[getFullStart(), max(getStart(), 每个装饰器/修饰符的 end))` —— 上界越过装饰器，
 * 这样两种书写位置都落在同一个区间里；下界取 `getFullStart()` 才能吃进前导 trivia。
 */
function docOf(p_node: ts.Node, p_sourceFile: ts.SourceFile): { brief: string; description: string } {
	const modifiers: readonly ts.Node[] = [
		...(ts.canHaveDecorators(p_node) ? ts.getDecorators(p_node) ?? [] : []),
		...((p_node as ts.HasModifiers).modifiers ?? []),
	];
	let upper = p_node.getStart(p_sourceFile);
	for (const modifier of modifiers) {
		upper = Math.max(upper, modifier.getEnd());
	}
	const lower = p_node.getFullStart();
	if (upper <= lower) {
		return EMPTY_DOC;
	}
	const span = p_sourceFile.getFullText().slice(lower, upper);

	// 区间内最后一段块注释。
	const matches = Array.from(span.matchAll(/\/\*\*[\s\S]*?\*\//g));
	if (matches.length === 0) {
		return EMPTY_DOC;
	}
	const last = matches[matches.length - 1];

	// 注释末端到声明起点之间若有空行 ⇒ 判为没有文档。
	//
	// 同时必须**排除其它注释**：TS 会把两条紧邻的注释合成一个 span
	// （`/** 孤儿说明 */` 与成员文档之间无空行时，`getFullStart()` 会一并吃进来），
	// 那样签名就会挂到一段不指向任何声明的注释上。只承认**直接贴合声明**的那一段：
	// 它前面若还有别的注释，说明它自己不是紧邻声明的那条。
	const trailing = span.slice((last.index ?? 0) + last[0].length);
	if (/(\r?\n[ \t]*){2,}/.test(trailing)) {
		return EMPTY_DOC;
	}
	const before = span.slice(0, last.index);
	if (/\*\/[ \t]*$/.test(before)) {
		return EMPTY_DOC;
	}

	const text = unwrapComment(last[0]);
	if (text.trim() === "") {
		return EMPTY_DOC;
	}
	return splitBrief(text);
}

/** 命名空间合并形态（`namespace C { export const MAX = 1 }`）不入成员表：常量在类体里声明。 */
function isSignalMember(p_member: ts.ClassElement): boolean {
	if (!ts.isPropertyDeclaration(p_member) || !p_member.name || !ts.isIdentifier(p_member.name)) {
		return false;
	}
	const typeNode = p_member.type;
	return !!typeNode && ts.isTypeReferenceNode(typeNode) && typeNode.typeName.getText() === "Signal";
}

function memberKind(p_member: ts.ClassElement): DocKind | undefined {
	if (ts.isMethodDeclaration(p_member) || ts.isGetAccessorDeclaration(p_member) || ts.isSetAccessorDeclaration(p_member)) {
		return DOC_KIND_METHOD;
	}
	if (ts.isPropertyDeclaration(p_member)) {
		if (isSignalMember(p_member)) {
			return DOC_KIND_SIGNAL;
		}
		// `static readonly X = 1` 被 `@bind.exposed.const()` 收集成常量；其余是导出的属性。
		const isStatic = (ts.canHaveModifiers(p_member) ? ts.getModifiers(p_member) : undefined)
			?.some((modifier) => modifier.kind === ts.SyntaxKind.StaticKeyword) ?? false;
		const isReadonly = (ts.canHaveModifiers(p_member) ? ts.getModifiers(p_member) : undefined)
			?.some((modifier) => modifier.kind === ts.SyntaxKind.ReadonlyKeyword) ?? false;
		return isStatic && isReadonly ? DOC_KIND_CONSTANT : DOC_KIND_PROPERTY;
	}
	return undefined;
}

/**
 * 收集一个类（含其成员）的文档。
 *
 * 类级文档与成员级走**同一条**规则；成员名用**源码成员名逐字**，与运行时
 * `script_class_info_.methods` / `.properties` 的登记键同源（见 design.md §2.3）。
 */
export function collectScriptDoc(p_sourceFile: ts.SourceFile): ScriptDoc | undefined {
	for (const statement of p_sourceFile.statements) {
		if (!ts.isClassDeclaration(statement) || !statement.name) {
			continue;
		}
		const classDoc = docOf(statement, p_sourceFile);
		const members: MemberDoc[] = [];
		for (const member of statement.members) {
			if (ts.isConstructorDeclaration(member)) {
				continue;
			}
			const kind = memberKind(member);
			if (kind === undefined) {
				continue;
			}
			const name = member.name && ts.isIdentifier(member.name) ? member.name.text : undefined;
			if (name === undefined) {
				continue;
			}
			const doc = docOf(member, p_sourceFile);
			if (doc.brief === "" && doc.description === "") {
				continue;
			}
			members.push({ kind, name, brief: doc.brief, description: doc.description });
		}
		return { class: { brief: classDoc.brief, description: classDoc.description }, members };
	}
	return undefined;
}

/** 从一段源码文本提取文档（`p_fileName` 只用于诊断与 `ScriptKind` 推断）。 */
export function collectScriptDocFromText(p_fileName: string, p_source: string): ScriptDoc | undefined {
	const isJavaScript = p_fileName.endsWith(".js") || p_fileName.endsWith(".cjs") || p_fileName.endsWith(".mjs");
	const sourceFile = ts.createSourceFile(
		p_fileName,
		p_source,
		ts.ScriptTarget.ES2022,
		true,
		isJavaScript ? ts.ScriptKind.JS : ts.ScriptKind.TS,
	);
	return collectScriptDoc(sourceFile);
}
