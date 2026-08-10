#include "jsb_node_pch.h"

#include "jsb_node_bridge.h"

#include <godot_cpp/core/defs.hpp>

// The native-addon probe entry exported by the main DLL.
//
// child_process.fork() probes (e.g. node-llama-cpp's gpu detection) start the
// bundled helper executable (jsb_node_host_main.cpp), which forwards straight
// here. We prepare the native-addon host (load the node.dll shim next to this
// module / promote this module's N-API symbols) and then run a real node
// process via node::Start. libnode is statically linked into this DLL, so the
// helper executable itself only needs to link this one exported function.
extern "C" {

int GDE_EXPORT godotjs_node_probe_main(int p_argc, char **p_argv) {
	jsb::impl::NodeBridge::PrepareNativeAddonHost();
	return node::Start(p_argc, p_argv);
}
}
