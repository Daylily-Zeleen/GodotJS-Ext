#pragma once
#include "jsb_v8_pch.h"

namespace jsb::impl {
struct GlobalInitialize {
#if JSB_V8_CPPGC
	static inline std::unique_ptr<cppgc::DefaultPlatform> platform{};
#else
	static inline std::unique_ptr<v8::Platform> platform{};
#endif

public:
	static v8::Platform *get_platform() {
#if JSB_V8_CPPGC
		return platform->GetV8Platform();
#else
		return platform.get();
#endif
	}

	static void init();
	static void shutdown();
};

} //namespace jsb::impl
