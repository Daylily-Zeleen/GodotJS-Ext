#pragma once

#include <compat/jsb_compat.h>

#include <bridge/jsb_statistics.h>

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/variant.hpp>

#define JSB_DECLARE_MONITOR(MonitorName) static Variant get_value_##MonitorName()

class GodotJSMonitor {
private:
	static jsb::Statistics stats_;
	static uint64_t last_flush_tick_;

protected:
	static void flush();

	JSB_DECLARE_MONITOR(objects);
	JSB_DECLARE_MONITOR(native_classes);
	JSB_DECLARE_MONITOR(script_classes);
	JSB_DECLARE_MONITOR(cached_string_names);
	JSB_DECLARE_MONITOR(persistent_objects);
	JSB_DECLARE_MONITOR(allocated_variants);

#if JSB_WITH_V8
	JSB_DECLARE_MONITOR(heap_size);
#elif JSB_WITH_QUICKJS
	JSB_DECLARE_MONITOR(memory_used_size);
#endif

public:
	static void register_monitors();
	static void unregister_monitors();
};

