#include "jsb_script_extension.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/property_info.hpp>

#include "jsb_script_instance.h"

namespace jsb::editor {

static HashMap<GodotJSScript *, PropertyInfo> script_categories;

static void register_script_extension() {
	GodotJSScript::callbacks.on_destruct = [](GodotJSScript *p_script) {
		script_categories.erase(p_script);
	};

	GDExtensionScriptInstanceInfo3 *script_info = ScriptInstance::_get_raw_script_instance_info();

	script_info->get_class_category_func = [](GDExtensionScriptInstanceDataPtr p_instance, GDExtensionPropertyInfo *p_class_category) {
		const ScriptInstance *script_instance = (ScriptInstance *)p_instance;
		GodotJSScript *script = script_instance->get_script().ptr();
		PropertyInfo &category = script_categories[script];

		String path = script->get_path();
		String scr_name;

		if (script->is_built_in()) {
			if (script->get_name().is_empty()) {
				scr_name = TTR("Built-in script");
			} else {
				scr_name = vformat("%s (%s)", script->get_name(), TTR("Built-in"));
			}
		} else {
			if (script->get_name().is_empty()) {
				scr_name = path.get_file();
			} else {
				scr_name = script->get_name();
			}
		}
		category.type = Variant::NIL;
		category.usage = PROPERTY_USAGE_CATEGORY;
		category.name = scr_name;
		category.hint = PROPERTY_HINT_NONE;
		category.hint_string = path;

		*p_class_category = category._to_gdextension();
		return (GDExtensionBool) true;
	};
}

static void unregister_script_extension() {
	GodotJSScript::callbacks.on_destruct = nullptr;
	GDExtensionScriptInstanceInfo3 *script_info = ScriptInstance::_get_raw_script_instance_info();
	script_info->get_class_category_func = nullptr;
}

} //namespace jsb::editor