// Fork-probe child for the node leg — mirrors Gode's
// `test/fixtures/npm_native_llama/scripts/fork_probe_child.cjs`.
//
// It reports its own process.execPath over IPC so the parent can assert the fork
// redirect picked the bundled `godotjs-ext` helper. An embedded libnode reports
// the *Godot* executable as execPath, so without the redirect in
// jsb_node_runtime.cpp the parent would see Godot here.
//
// Must stay CommonJS (.cjs): it is launched by node itself, not by the GodotJS
// bridge loader.
//
// 编辑器生成 typings 时会扫到并执行本工程里的 .cjs，那时全局 `require` 是 GodotJS 的桥接
// require（只认 GodotJS 模块），且 `process` 未定义 —— 裸跑本文件会抛
// `ReferenceError: process is not defined`，编辑器把它记成
// `failed to attach module res://tests/node-runtime/fork-probe-child.cjs`。
// 可靠判据是桥接 require **没有 `resolve`**（与 `native-addon-probe.cjs` 同款）：
// 不能靠 `__godotjs_node_require` 判断，node 腿的 Godot 进程里它也存在。
// 非真 node 时立即返回，脚本变成无害的空壳。

const isBridgeRequire = typeof require !== "function" || typeof require.resolve !== "function";
if (!isBridgeRequire && process.send) {
	process.send({
		type: "node-fork-probe",
		execPath: process.execPath,
		version: process.version,
	});
}
