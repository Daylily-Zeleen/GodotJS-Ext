#pragma once

#include "jsb_node_pch.h"

namespace jsb::impl {
// The 'godot' linked binding exposed to the node runtime. It provides the
// low-level helpers the bootstrap script uses to wire res:// aware `fs`
// and `process.dlopen` for native `.node` addons.
struct NodeBridge {
	// prepare the host process for loading native .node addons:
	// - Windows: preload the node.dll shim next to this module (the addon's
	//   napi_* imports are forwarded through the shim)
	// - Linux/macOS/Android: promote this module's N-API symbols to global
	//   visibility (libnode is statically linked, not globally visible by default)
	static void PrepareNativeAddonHost();

	// register the 'godot' linked binding on the node environment.
	// After this call, `internalBinding('godot')` (and `require('godot')` after
	// the bootstrap patches the module loader) returns an object with
	// `fs_readFile`, `fs_stat` and `preload_dlls`.
	static void AddGodotLinkedBinding(node::Environment *p_env);
};

} //namespace jsb::impl
