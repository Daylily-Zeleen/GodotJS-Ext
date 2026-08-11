#include "jsb_node_global_init.h"

#include <uv.h>

namespace jsb::impl {

void GlobalInitialize::init() {
	jsb_check(GlobalInitialize::platform == nullptr);

	uv_replace_allocator(
			[](size_t size) { return memalloc(size); },
			[](void *ptr, size_t size) { return memrealloc(ptr, size); },
			[](size_t count, size_t size) { return memalloc(count * size); },
			[](void *ptr) { memfree(ptr); });

	// Process-wide Node.js initialization. We skip the parts that would conflict
	// with the host engine (Godot):
	//   - kNoInitializeV8:               we call v8::V8::Initialize() ourselves below
	//   - kNoInitializeNodeV8Platform:   we create our own node::MultiIsolatePlatform
	//   - kNoInitializeCppgc:            cppgc is not used (JSB_V8_CPPGC = 0)
	//   - kNoDefaultSignalHandling:      do not install Node signal handlers (Godot owns them)
	//   - kNoStdioInitialization:        do not touch stdio/TTY state
	const std::vector<std::string> args = { "godotjs-ext", "--experimental-vm-modules" };
	// note: use the initializer_list overload; bitwise-OR of the enum values would
	// promote to int and fail to convert back to Flags (error C2665).
	node::InitializeOncePerProcess(args, {
												 node::ProcessInitializationFlags::kNoInitializeV8,
												 node::ProcessInitializationFlags::kNoInitializeNodeV8Platform,
												 node::ProcessInitializationFlags::kNoInitializeCppgc,
												 node::ProcessInitializationFlags::kNoDefaultSignalHandling,
												 node::ProcessInitializationFlags::kNoStdioInitialization,
										 });

	// the shared multi-isolate platform (thread pool of 4 workers, like gode)
	GlobalInitialize::platform = std::move(node::MultiIsolatePlatform::Create(4));
	jsb_ensure(GlobalInitialize::platform.get());

	v8::V8::InitializePlatform(GlobalInitialize::platform.get());
	v8::V8::Initialize();

	jsb_ensure(get_platform());
}

void GlobalInitialize::shutdown() {
	jsb_check(GlobalInitialize::platform);
	uv_library_shutdown();

	v8::V8::Dispose();
	v8::V8::DisposePlatform();
	node::TearDownOncePerProcess();
	GlobalInitialize::platform.reset();
}

} //namespace jsb::impl
