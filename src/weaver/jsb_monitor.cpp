#include "jsb_monitor.h"
#include "../internal/jsb_internal.h"
#include "jsb_script_language.h"


#include <godot_cpp/classes/performance.hpp>

#define JSB_REGISTER_MONITOR(MonitorName) \
	Performance::get_singleton()->add_custom_monitor(JSB_MODULE_NAME_STRING "/" #MonitorName, callable_mp_static(&GodotJSMonitor::get_value_##MonitorName), {})
#define JSB_UNREGISTER_MONITOR(MonitorName) \
	Performance::get_singleton()->remove_custom_monitor(JSB_MODULE_NAME_STRING "/" #MonitorName);

#define JSB_DEFINE_MONITOR(MonitorName)                 \
	Variant GodotJSMonitor::get_value_##MonitorName() { \
		flush();                                        \
		return stats_.MonitorName;                      \
	}

#define JSB_DEFINE_CUSTOM_MONITOR(MonitorName, Accessor)       \
	Variant GodotJSMonitor::get_value_##MonitorName() {        \
		flush();                                               \
		return stats_.get_custom_field(#MonitorName).Accessor; \
	}

jsb::Statistics GodotJSMonitor::stats_{};
uint64_t GodotJSMonitor::last_flush_tick_{ 0 };

JSB_DEFINE_MONITOR(objects);
JSB_DEFINE_MONITOR(native_classes);
JSB_DEFINE_MONITOR(script_classes);
JSB_DEFINE_MONITOR(cached_string_names);
JSB_DEFINE_MONITOR(persistent_objects);
JSB_DEFINE_MONITOR(allocated_variants);

#if JSB_WITH_V8
JSB_DEFINE_CUSTOM_MONITOR(heap_size, u.u64_cap[0]);
#elif JSB_WITH_QUICKJS
JSB_DEFINE_CUSTOM_MONITOR(memory_used_size, u.i64);
#endif

void GodotJSMonitor::register_monitors() {
	JSB_REGISTER_MONITOR(objects);
	JSB_REGISTER_MONITOR(native_classes);
	JSB_REGISTER_MONITOR(script_classes);
	JSB_REGISTER_MONITOR(cached_string_names);
	JSB_REGISTER_MONITOR(persistent_objects);
	JSB_REGISTER_MONITOR(allocated_variants);
#if JSB_WITH_V8
	JSB_REGISTER_MONITOR(heap_size);
#elif JSB_WITH_QUICKJS
	JSB_REGISTER_MONITOR(memory_used_size);
#endif
}

void GodotJSMonitor::unregister_monitors() {
	JSB_UNREGISTER_MONITOR(objects);
	JSB_UNREGISTER_MONITOR(native_classes);
	JSB_UNREGISTER_MONITOR(script_classes);
	JSB_UNREGISTER_MONITOR(cached_string_names);
	JSB_UNREGISTER_MONITOR(persistent_objects);
	JSB_UNREGISTER_MONITOR(allocated_variants);
#if JSB_WITH_V8
	JSB_UNREGISTER_MONITOR(heap_size);
#elif JSB_WITH_QUICKJS
	JSB_UNREGISTER_MONITOR(memory_used_size);
#endif
}

void GodotJSMonitor::flush() {
	const uint64_t ticks = Time::get_singleton()->get_ticks_usec();
	if (ticks - last_flush_tick_ < 1000ULL) {
		return;
	}

	last_flush_tick_ = ticks;
	const GodotJSScriptLanguage *lang = GodotJSScriptLanguage::get_singleton();
	if (!lang)
		return;
	const std::shared_ptr<jsb::Environment> env = lang->get_environment();
	if (!env)
		return;
	env->get_statistics(stats_);
}
