/*
 *  jsb.editor.tools.cts
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
 *
 *  You should have received a copy of the GNU Lesser General Public    *
 *  License along with this library; if not,                            *
 *  see <https://www.gnu.org/licenses/>.                                *
 */

/**
 * 常驻编辑器工具进程（Node 运行，由编辑器扩展 spawn 并保持存活）。
 *
 * 为什么是常驻而不是短命进程：每次 spawn 都要重新 `require("typescript")`
 * （数十 MB JS 解析 + JIT），而它同时服务签名提取与帮助文档提取。
 *
 * 协议（NDJSON，一行一条；**宿主写 stdin，工具写 stdout**）：
 *
 *   请求： {"id":17,"op":"signatures"}                       全项目签名提取（离线产物）
 *         {"id":18,"op":"doc","paths":["res://a.ts", ...]}  单文件帮助文档（内存即答）
 *   应答： {"id":17,"ok":true,"written":18,"up-to-date":0}
 *         {"id":18,"ok":true,"docs":{"res://a.ts":{...}}}
 *         {"id":18,"ok":false,"error":"..."}
 *
 * 约束：
 *  - **stdout 只承载协议报文**。任何诊断都写 stderr（宿主会把它当日志行丢弃/打印），
 *    否则杂音会破坏 NDJSON 流。
 *  - 未知 `op`、坏 JSON 一律回 `ok:false` + 说明，**不退出进程**。
 */

import * as fs from "node:fs";
import * as path from "node:path";
import * as crypto from "node:crypto";
import * as readline from "node:readline";

// NOTE specifiers must carry the **emitted** extension (`.cjs`): the sources are `.cts`, and a `.cts`
// file compiles to `.cjs`, so an extensionless `./x` would look for a `x.js` that never exists
// (实测：extensionless 与 `./x.js` 都报 TS2307；只有 `./x.cjs` 编译通过且 emit 出的 require 可运行)。
import { runExtraction, listGodotScripts } from "./jsb.signature.extract.cjs";
import { collectScriptDocFromText, ScriptDoc } from "./jsb.doc.extract.cjs";

const SIG_EXT = ".sig";

interface Request {
	id: number;
	op: string;
	project?: string;
	paths?: string[];
	/** `op:"doc"` 的全项目模式（编辑器推模式的唯一用法）。 */
	all?: boolean;
}

/** 一份缓存条目：源文件 md5 + 解析结果（无文档时为 undefined，同样要缓存以免反复解析）。 */
interface CachedDoc {
	md5: string;
	doc: ScriptDoc | undefined;
}

/** 进程内文档缓存：绝对路径 -> `{ md5, doc }`。md5 变了就重解析（= 刷新机制 L1）。 */
const docCache = new Map<string, CachedDoc>();

/** 项目根（来自 `--project`，绝对路径）。`res://` 请求按它归一。 */
let projectRoot = process.cwd();

/** 分派一个请求，返回要写回宿主的报文（不含 `id`）。 */
function dispatch(p_request: Request): Record<string, unknown> {
	try {
		switch (p_request.op) {
			case "ping":
				return { ok: true, pong: true };
			case "signatures":
				return opSignatures(p_request);
			case "doc":
				return opDoc(p_request);
			default:
				return { ok: false, error: "unknown op: " + String(p_request.op) };
		}
	} catch (error) {
		return { ok: false, error: error instanceof Error ? error.message : String(error) };
	}
}

function opSignatures(p_request: Request): Record<string, unknown> {
	// 默认用 `--project` 给的根：请求里省掉该字段时不能用 cwd（它只是工具进程的启动目录）。
	const project = p_request.project ?? projectRoot;
	const result = runExtraction({ project, dump: false, verbose: false }, false);
	const messages = result.messages.join("\n");
	if (!result.ok) {
		return { ok: false, error: messages };
	}
	return {
		ok: true,
		scanned: result.scanned,
		written: result.written,
		"up-to-date": result.upToDate,
		empty: result.empty,
		ignored: result.ignored,
		"non-godot": result.nonGodot,
	};
}

function opDoc(p_request: Request): Record<string, unknown> {
	const project = p_request.project ?? projectRoot;
	// `all` = 全项目（编辑器推模式下唯一的用法）；否则只处理显式点名的 `paths`。
	const requested: string[] = p_request.all
		? listGodotScripts(project).map((entry) => entry.resPath)
		: p_request.paths ?? [];

	const docs: Record<string, ScriptDoc | null> = {};
	const errors: string[] = [];
	for (const requestPath of requested) {
		try {
			// 没有文档时写 null（而不是省略 key）：宿主据此区分"问过了、没有文档"与"没问过"。
			docs[requestPath] = docForPath(project, requestPath) ?? null;
		} catch (error) {
			// 单个文件失败不该让整批失败：报出来并继续（宿主侧会把它当"这个文件没文档"）。
			errors.push(requestPath + ": " + (error instanceof Error ? error.message : String(error)));
			docs[requestPath] = null;
		}
	}
	if (errors.length > 0) {
		return { ok: false, error: errors.join("\n"), docs };
	}
	return { ok: true, docs };
}

function docForPath(p_project: string, p_requestPath: string): ScriptDoc | undefined {
	const absolute = path.resolve(projectPathToAbsolute(p_project, p_requestPath));
	const source = fs.readFileSync(absolute, "utf8");
	const md5 = crypto.createHash("md5").update(source, "utf8").digest("hex");
	const cached = docCache.get(absolute);
	if (cached !== undefined && cached.md5 === md5) {
		return cached.doc; // 刷新机制 L1：md5 未变直接回缓存，不重复解析。
	}
	const doc = collectScriptDocFromText(absolute, source);
	docCache.set(absolute, { md5, doc });
	return doc;
}

/**
 * `res://x/y.ts` -> 绝对路径。
 *
 * `res://` 直接映射到 `--project` 给的项目根；已经是绝对路径的请求原样返回。
 */
function projectPathToAbsolute(p_project: string, p_requestPath: string): string {
	if (p_requestPath.startsWith("res://")) {
		return path.join(p_project, p_requestPath.slice("res://".length));
	}
	if (p_requestPath.startsWith("user://")) {
		// 不该出现；`user://` 在项目之外，无法从 `--project` 推导。显式报错优于静默猜错。
		throw new Error("user:// paths are not supported: " + p_requestPath);
	}
	return p_requestPath;
}

/** CLI：`node jsb.editor.tools.cjs --project <abs>`。 */
function parseArgs(p_argv: readonly string[]): void {
	for (let i = 0; i < p_argv.length; ++i) {
		if (p_argv[i] === "--project" && i + 1 < p_argv.length) {
			projectRoot = path.resolve(p_argv[++i]);
		}
	}
}

function writeResponse(p_id: number, p_payload: Record<string, unknown>): void {
	// stdout 是协议通道：任何非 NDJSON 内容都会破坏它，所以这里只写一行 JSON。
	process.stdout.write(JSON.stringify({ id: p_id, ...p_payload }) + "\n");
}

function main(): void {
	parseArgs(process.argv.slice(2));
	process.stderr.write("[editor-tools] started, project=" + projectRoot + "\n");
	// 启动即加载 TS 编译器一次：这是常驻形态的主要收益（短命进程每次都要重付）。
	require("typescript");

	const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
	rl.on("line", (line: string) => {
		const trimmed = line.trim();
		if (trimmed === "") {
			return;
		}
		let request: Request;
		try {
			request = JSON.parse(trimmed) as Request;
		} catch (error) {
			// 没有 id 可回显：写到 stderr，宿主会在日志里看到。
			process.stderr.write("[editor-tools] malformed request line: " + trimmed + "\n");
			return;
		}
		if (typeof request.id !== "number") {
			process.stderr.write("[editor-tools] request without a numeric id, ignored\n");
			return;
		}
		writeResponse(request.id, dispatch(request));
	});

	// stdin 关闭 = 宿主退出（或主动关管道）：跟随退出，避免留下孤儿进程。
	rl.on("close", () => {
		process.exit(0);
	});
}

main();

// 供将来的单测直接调用（不经过 stdin）。
export { dispatch, docForPath, SIG_EXT };
