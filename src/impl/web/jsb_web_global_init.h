#pragma once
#include "jsb_web_pch.h"

namespace jsb::impl {
struct GlobalInitialize {
	GlobalInitialize();

	static void init();

	static void shutdown();
};

} //namespace jsb::impl
