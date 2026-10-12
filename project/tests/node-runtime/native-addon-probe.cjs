// native-addon-probe.cjs
//
// This file is part of:
//                               GodotJS-Ext
//             https://github.com/Daylily-Zeleen/GodotJS-Ext
//
// 在 forked helper（真 node 进程）里加载 node-llama-cpp 自带的 llama.cpp 原生 addon，
// 结果写入文件：fork 重定向不保证 IPC 可用，文件也更便于手工诊断。
//
// 编辑器生成 typings 时会扫到并执行本工程里的 .cjs，那时全局 `require` 是 GodotJS 的桥接
// require（只认 GodotJS 模块，遇到 "node:fs" 会崩）。注意不能靠 __godotjs_node_require 判断：
// node 腿的 Godot 进程里它也存在。可靠的判据是桥接 require **没有 `resolve`**。
// 非真 node 时立即返回，脚本变成无害的空壳。

const isBridgeRequire = typeof require !== "function" || typeof require.resolve !== "function";
if (isBridgeRequire) {
	// 桥接环境：什么都不做（见上）。
} else {
	const fs = require("node:fs");
	const path = require("node:path");
	const { createRequire } = require("node:module");

	// 预编译二进制在 `@node-llama-cpp/<platform>` 里。pnpm 不会把它放到顶层 node_modules，
	// 所以从包自身导出的模块反推包目录，再在它旁边找平台包；同时也试提升后的位置。
	const platformDir = {
		"win32-x64": "win-x64",
		"win32-arm64": "win-arm64",
		"darwin-x64": "mac-x64",
		"darwin-arm64": "mac-arm64",
		"linux-x64": "linux-x64",
		"linux-arm64": "linux-arm64",
	}[`${process.platform}-${process.arch}`];

	const packageRequire = createRequire(path.join(process.cwd(), "node_modules", "node-llama-cpp", "package.json"));

	const candidateAddonPaths = [];
	if (platformDir) {
		const relative = path.join("@node-llama-cpp", platformDir, "bins", platformDir, "llama-addon.node");
		candidateAddonPaths.push(path.join(process.cwd(), "node_modules", relative));
		try {
			const packageDir = path.dirname(path.dirname(packageRequire.resolve("./dist/index.js")));
			candidateAddonPaths.push(path.resolve(packageDir, "..", relative));
		} catch (_) {}
	}

	const resultPath = path.join(process.cwd(), ".native-addon-probe.json");
	const addonPath = candidateAddonPaths.find((candidate) => fs.existsSync(candidate)) ?? "";

	let result;
	try {
		if (addonPath.length === 0) {
			throw new Error("no prebuilt llama addon found; tried:\n" + candidateAddonPaths.join("\n"));
		}
		// addon 的同级 DLL（ggml-*.dll）由平台加载器在装载时解析，先把该目录放进搜索路径。
		process.env.PATH = path.dirname(addonPath) + path.delimiter + (process.env.PATH || "");
		const addon = require(addonPath);
		result = { ok: true, addonType: typeof addon, addonPath };
	} catch (error) {
		result = { ok: false, error: String(error && error.message ? error.message : error), addonPath };
	}

	try {
		fs.writeFileSync(resultPath, JSON.stringify(result));
	} catch (error) {
		console.error("[native-addon-probe] could not write " + resultPath + ": " + String(error));
		process.exit(2);
	}
}
