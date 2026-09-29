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
if (process.send) {
	process.send({
		type: "node-fork-probe",
		execPath: process.execPath,
		version: process.version,
	});
}
