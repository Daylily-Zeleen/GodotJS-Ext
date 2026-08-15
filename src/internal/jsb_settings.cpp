#include "jsb_settings.h"

#include "jsb_internal_pch.h"
#include "jsb_logger.h"
#include "jsb_macros.h"

#include <godot_cpp/classes/resource_uid.hpp>

#include <compat/project_settings.h>

#ifdef TOOLS_ENABLED
#	include "../weaver-editor/jsb_editor_helper.h"
#	include <godot_cpp/classes/engine.hpp>
#endif // TOOLS_ENABLED

#define JSB_SET_RESTART(val) (val)
#define JSB_SET_IGNORE_DOCS(val) (val)
#define JSB_SET_BASIC(val) (val)
#define JSB_SET_INTERNAL(val) (val)

namespace jsb::internal {
#ifdef TOOLS_ENABLED
static constexpr char kEdDebuggerPort[] = JSB_MODULE_NAME_STRING "/debugger/editor_port";
static constexpr char kEdAutogenPath[] = JSB_MODULE_NAME_STRING "/codegen/autogen_path";
static constexpr char kEdAutogenSceneDTSSettings[] = JSB_MODULE_NAME_STRING "/codegen/autogen_scene_dts_settings";
static constexpr char kEdAutogenResourceDTSSettings[] = JSB_MODULE_NAME_STRING "/codegen/autogen_resource_dts_settings";
static constexpr char kEdCodegenUseProjectSettings[] = JSB_MODULE_NAME_STRING "/codegen/use_project_settings";
#endif

// use unnecessary first category layer (runtime and editor) to make the second layer shown as sections in project settings

static constexpr char kRtBridgeLoggingEnabled[] = JSB_MODULE_NAME_STRING "/runtime/bridge_logging_enabled";
static constexpr char kRtDebuggerPort[] = JSB_MODULE_NAME_STRING "/runtime/debugger/debugger_port";
static constexpr char kRtDebuggerSourceMapBaseUrl[] = JSB_MODULE_NAME_STRING "/runtime/debugger/source_map_base_url";
static constexpr char kRtDebuggerWaitForDebugger[] = JSB_MODULE_NAME_STRING "/runtime/debugger/wait_for_debugger";
static constexpr char kRtSourceMapEnabled[] = JSB_MODULE_NAME_STRING "/runtime/logger/source_map_enabled";
static constexpr char kRtAdditionalSearchPaths[] = JSB_MODULE_NAME_STRING "/runtime/core/additional_search_paths";
static constexpr char kRtEntryScriptPath[] = JSB_MODULE_NAME_STRING "/runtime/core/entry_script_path";
static constexpr char kRtCamelCaseBindingsEnabled[] = JSB_MODULE_NAME_STRING "/runtime/core/camel_case_bindings_enabled";

// editor specific settings, but we need it configured as project-wise instead of global-wise
static constexpr char kRtPackagingWithSourceMap[] = JSB_MODULE_NAME_STRING "/editor/packaging/source_map_included";
static constexpr char kRtPackagingIncludeFiles[] = JSB_MODULE_NAME_STRING "/editor/packaging/include_files";
static constexpr char kRtPackagingIncludeDirectories[] = JSB_MODULE_NAME_STRING "/editor/packaging/include_directories";
static constexpr char kRtPackagingReferencedNodeModules[] = JSB_MODULE_NAME_STRING "/editor/packaging/referenced_node_modules";

// ignored classes 是不生成对应的 .d.ts 声明代码，.d.ts 本身不被打包发布，但是哪些类应该生成那些类不该被生成也是项目特定的，不应该作为编辑器设置。
// 语义改为ignored classes 的子类也会被忽略
static constexpr char kRtIgnoredClasses[] = JSB_MODULE_NAME_STRING "/codegen/ignored_classes";

static constexpr char kRtResourceDTSIncludePathWildcards[] = JSB_MODULE_NAME_STRING "/codegen/resource_dts/include_path_wildcards";
static constexpr char kRtResourceDTSExcludePathWildcards[] = JSB_MODULE_NAME_STRING "/codegen/resource_dts/exclude_path_wildcards";
static constexpr char kRtSceneDTSIncludePathWildcards[] = JSB_MODULE_NAME_STRING "/codegen/scene_dts/include_path_wildcards";
static constexpr char kRtSceneDTSExcludePathWildcards[] = JSB_MODULE_NAME_STRING "/codegen/scene_dts/exclude_path_wildcards";

static constexpr char kRtSceneDTSGenerateStrategic[] = JSB_MODULE_NAME_STRING "/codegen/scene_dts/generate_strategic";

static constexpr char kScriptInlineResourceUID[] = JSB_MODULE_NAME_STRING "/editor/script/inline_uid";

#ifdef TOOLS_ENABLED
bool init_editor_settings() {
	static bool inited = false;
	if (!inited) {
		if (get_editor_settings().is_null()) {
			if (Engine::get_singleton()->is_editor_hint()) {
				CRASH_COND_MSG(get_editor_settings() == nullptr, "EditorSettings is unavailable.");
			} else {
				JSB_LOG(Verbose, "EditorSettings is not available when initialising %s", jsb_typename(jsb::internal::Settings));
			}
		}

		// check before read to avoid redundant warnings
		if (Ref<EditorSettings> editor_settings = get_editor_settings(); editor_settings.is_valid()) {
			inited = true;
			_EDITOR_DEF(kEdDebuggerPort, 9230, true);
			_EDITOR_DEF(kEdAutogenPath, "gen/godot", false);
			{
				PropertyInfo AutogenSceneDTSSettings;
				AutogenSceneDTSSettings.type = Variant::INT;
				AutogenSceneDTSSettings.name = kEdAutogenSceneDTSSettings;
				AutogenSceneDTSSettings.hint = PROPERTY_HINT_FLAGS;
				AutogenSceneDTSSettings.usage = PROPERTY_USAGE_NONE;

				// NOTE: Keep this map sync with jsb::internal::AutoGenSettingFlags
				const Pair<String, AutoGenSettingFlags> options[]{
					{ "Enabled", AutoGenSettingFlags::ENABLED },
					{ "Generate On Save", AutoGenSettingFlags::GEN_ON_SAVE },
					{ "Changed Files only", AutoGenSettingFlags::CHANGED_FILE_ONLY },
				};

				PackedStringArray flag_hints;
				for (const auto &[name, value] : options) {
					flag_hints.push_back(vformat("%s:%s", name, int64_t(value)));
				}

				AutogenSceneDTSSettings.hint_string = String(",").join(flag_hints);
				_EDITOR_DEF(kEdAutogenSceneDTSSettings, int64_t(ENABLED | GEN_ON_SAVE | CHANGED_FILE_ONLY), false, JSB_SET_BASIC(true));
				editor_settings->add_property_info(AutogenSceneDTSSettings);
			}

			{
				PropertyInfo AutogenResourceDTSSettings;
				AutogenResourceDTSSettings.type = Variant::INT;
				AutogenResourceDTSSettings.name = kEdAutogenSceneDTSSettings;
				AutogenResourceDTSSettings.hint = PROPERTY_HINT_FLAGS;
				AutogenResourceDTSSettings.usage = PROPERTY_USAGE_NONE;

				// NOTE: Keep this map sync with jsb::internal::AutoGenSettingFlags
				const Pair<String, AutoGenSettingFlags> options[]{
					{ "Enabled", AutoGenSettingFlags::ENABLED },
					{ "Generate On Save", AutoGenSettingFlags::GEN_ON_SAVE },
					{ "Changed Files only", AutoGenSettingFlags::CHANGED_FILE_ONLY },
				};

				PackedStringArray flag_hints;
				for (const auto &[name, value] : options) {
					flag_hints.push_back(vformat("%s:%s", name, int64_t(value)));
				}

				AutogenResourceDTSSettings.hint_string = String(",").join(flag_hints);
				_EDITOR_DEF(kEdAutogenResourceDTSSettings, int64_t(ENABLED | GEN_ON_SAVE | CHANGED_FILE_ONLY), false, JSB_SET_BASIC(true));
				editor_settings->add_property_info(AutogenResourceDTSSettings);
			}
			_EDITOR_DEF(kEdCodegenUseProjectSettings, true, false);
		}
	}
	return inited;
}
#endif

void init_settings() {
	static bool inited = false;
	if (!inited) {
		inited = true;
		static constexpr char filter[] = "*." JSB_JAVASCRIPT_EXT ",*." JSB_COMMONJS_EXT ",*." JSB_MODULE_EXT
#if JSB_USE_TYPESCRIPT
										 ",*." JSB_TYPESCRIPT_EXT
#endif
				;

		_GLOBAL_DEF(kRtDebuggerPort, 9229, JSB_SET_RESTART(true), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(false), JSB_SET_INTERNAL(false));
		_GLOBAL_DEF(kRtDebuggerSourceMapBaseUrl, "http://localhost:9230", JSB_SET_RESTART(false), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(false), JSB_SET_INTERNAL(false));
		_GLOBAL_DEF(kRtDebuggerWaitForDebugger, false, JSB_SET_RESTART(false), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
		_GLOBAL_DEF(kRtSourceMapEnabled, true, JSB_SET_RESTART(false), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
		_GLOBAL_DEF(kRtAdditionalSearchPaths, PackedStringArray(), JSB_SET_RESTART(false), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
		_GLOBAL_DEF(kRtCamelCaseBindingsEnabled, false, JSB_SET_RESTART(true), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));

		{
			PropertyInfo EntryScriptPath;
			EntryScriptPath.type = Variant::STRING;
			EntryScriptPath.name = kRtEntryScriptPath;
			EntryScriptPath.hint = PROPERTY_HINT_FILE;
			EntryScriptPath.hint_string = filter;
			_GLOBAL_DEF(EntryScriptPath, String(), JSB_SET_RESTART(false), JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
		}

		_GLOBAL_DEF(kRtPackagingWithSourceMap, true, false);

		{
			PropertyInfo PackagingIncludeFiles;
			PackagingIncludeFiles.type = Variant::ARRAY;
			PackagingIncludeFiles.name = kRtPackagingIncludeFiles;
			PackagingIncludeFiles.hint = PROPERTY_HINT_ARRAY_TYPE;
			PackagingIncludeFiles.hint_string = vformat("%s/%s:%s", Variant::STRING, PROPERTY_HINT_FILE, filter);
			_GLOBAL_DEF(PackagingIncludeFiles, Array(), false, JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
		}

		{
			PropertyInfo PackagingIncludeDirectories;
			PackagingIncludeDirectories.type = Variant::ARRAY;
			PackagingIncludeDirectories.name = kRtPackagingIncludeDirectories;
			PackagingIncludeDirectories.hint = PROPERTY_HINT_ARRAY_TYPE;
			PackagingIncludeDirectories.hint_string = vformat("%s/%s:%s", Variant::STRING, PROPERTY_HINT_DIR, filter);
			_GLOBAL_DEF(PackagingIncludeDirectories, Array(), false, JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
		}

		_GLOBAL_DEF(kRtPackagingReferencedNodeModules, true, false);
		_GLOBAL_DEF(kRtBridgeLoggingEnabled, false, false);

		_GLOBAL_DEF(kRtIgnoredClasses, PackedStringArray(), false);
		{
			PropertyInfo ResourceDTSIncludePathWildcards;
			ResourceDTSIncludePathWildcards.type = Variant::PACKED_STRING_ARRAY;
			ResourceDTSIncludePathWildcards.name = kRtResourceDTSIncludePathWildcards;
			ResourceDTSIncludePathWildcards.hint = PROPERTY_HINT_ARRAY_TYPE;
			ResourceDTSIncludePathWildcards.hint_string = vformat("%s/%s:", Variant::STRING, PROPERTY_HINT_DIR);
			_GLOBAL_DEF(ResourceDTSIncludePathWildcards, PackedStringArray{ "res://" }, false, JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
		}

		{
			PropertyInfo ResourceDTSExcludePathWildcards;
			ResourceDTSExcludePathWildcards.type = Variant::PACKED_STRING_ARRAY;
			ResourceDTSExcludePathWildcards.name = kRtResourceDTSExcludePathWildcards;
			ResourceDTSExcludePathWildcards.hint = PROPERTY_HINT_ARRAY_TYPE;
			ResourceDTSExcludePathWildcards.hint_string = vformat("%s/%s:", Variant::STRING, PROPERTY_HINT_DIR);
			_GLOBAL_DEF(ResourceDTSExcludePathWildcards, PackedStringArray(), false, JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
		}

		{
			PropertyInfo SceneDTSIncludePathWildcards;
			SceneDTSIncludePathWildcards.type = Variant::PACKED_STRING_ARRAY;
			SceneDTSIncludePathWildcards.name = kRtSceneDTSIncludePathWildcards;
			SceneDTSIncludePathWildcards.hint = PROPERTY_HINT_ARRAY_TYPE;
			SceneDTSIncludePathWildcards.hint_string = vformat("%s/%s:", Variant::STRING, PROPERTY_HINT_DIR);
			_GLOBAL_DEF(SceneDTSIncludePathWildcards, PackedStringArray{ "res://" }, false, JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
		}

		{
			PropertyInfo SceneDTSExcludePathWildcards;
			SceneDTSExcludePathWildcards.type = Variant::PACKED_STRING_ARRAY;
			SceneDTSExcludePathWildcards.name = kRtSceneDTSExcludePathWildcards;
			SceneDTSExcludePathWildcards.hint = PROPERTY_HINT_ARRAY_TYPE;
			SceneDTSExcludePathWildcards.hint_string = vformat("%s/%s:", Variant::STRING, PROPERTY_HINT_DIR);
			_GLOBAL_DEF(SceneDTSExcludePathWildcards, PackedStringArray(), false, JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
		}

#ifdef TOOLS_ENABLED
		{
			using SceneDTSGenerateStrategicEnum = jsb::internal::SceneDTSGenerateStrategic;
			PropertyInfo SceneDTSGenerateStrategic;
			SceneDTSGenerateStrategic.type = Variant::INT;
			SceneDTSGenerateStrategic.name = kRtSceneDTSGenerateStrategic;
			SceneDTSGenerateStrategic.hint = PROPERTY_HINT_FLAGS;

			// NOTE: Keep this map sync with std::internal::SceneDTSGenerateStrategic
			const Pair<String, SceneDTSGenerateStrategicEnum> options[]{
				{ "Origin Name Node", SceneDTSGenerateStrategicEnum::ORIGIN_NAME_NODE },
				{ "Unique Name Node", SceneDTSGenerateStrategicEnum::UNIQUE_NAME_NODE }
			};

			PackedStringArray flag_hints;
			for (const auto &[name, value] : options) {
				flag_hints.push_back(vformat("%s:%s", name, int64_t(value)));
			}

			SceneDTSGenerateStrategic.hint_string = String(",").join(flag_hints);
			_GLOBAL_DEF(SceneDTSGenerateStrategic, int64_t(SceneDTSGenerateStrategicEnum::ORIGIN_NAME_NODE), false, JSB_SET_IGNORE_DOCS(false), JSB_SET_BASIC(true), JSB_SET_INTERNAL(false));
		}
#endif // TOOLS_ENABLED
		_GLOBAL_DEF(kScriptInlineResourceUID, true, false);
	}
}

#ifdef TOOLS_ENABLED
bool Settings::editor_settings_available() {
	return get_editor_settings().is_valid(); // || Engine::get_singleton()->is_editor_hint();
}

String Settings::get_autogen_path() {
	init_editor_settings();
	return EDITOR_GET(kEdAutogenPath);
}

BitField<AutoGenSettingFlags> Settings::get_autogen_scene_dts_settings() {
	init_editor_settings();
	return BitField<AutoGenSettingFlags>(EDITOR_GET(kEdAutogenSceneDTSSettings));
}

BitField<AutoGenSettingFlags> Settings::get_autogen_resource_dts_settings() {
	init_editor_settings();
	return BitField<AutoGenSettingFlags>(EDITOR_GET(kEdAutogenResourceDTSSettings));
}

bool Settings::get_codegen_use_project_settings() {
	init_editor_settings();
	return EDITOR_GET(kEdCodegenUseProjectSettings);
}
#endif

bool Settings::is_packaging_with_source_map() {
	init_settings();
	return GLOBAL_GET(kRtPackagingWithSourceMap);
}

PackedStringArray Settings::get_packaging_include_files() {
	init_settings();
	// rely on auto variant convert from Array
	return (PackedStringArray)GLOBAL_GET(kRtPackagingIncludeFiles);
}

PackedStringArray Settings::get_packaging_include_directories() {
	init_settings();
	// rely on auto variant convert from Array
	return (PackedStringArray)GLOBAL_GET(kRtPackagingIncludeDirectories);
}

bool Settings::is_packaging_referenced_node_modules() {
	init_settings();
	return GLOBAL_GET(kRtPackagingReferencedNodeModules);
}

bool Settings::is_bridge_logging_enabled() {
	init_settings();
	return GLOBAL_GET(kRtBridgeLoggingEnabled);
}

PackedStringArray Settings::get_resource_dts_include_path_wildcards() {
	init_settings();
	return (PackedStringArray)GLOBAL_GET(kRtResourceDTSIncludePathWildcards);
}

PackedStringArray Settings::get_resource_dts_exclude_path_wildcards() {
	init_settings();
	return (PackedStringArray)GLOBAL_GET(kRtResourceDTSExcludePathWildcards);
}

PackedStringArray Settings::get_scene_dts_include_path_wildcards() {
	init_settings();
	return (PackedStringArray)GLOBAL_GET(kRtSceneDTSIncludePathWildcards);
}

PackedStringArray Settings::get_scene_dts_exclude_path_wildcards() {
	init_settings();
	return (PackedStringArray)GLOBAL_GET(kRtSceneDTSExcludePathWildcards);
}

BitField<SceneDTSGenerateStrategic> Settings::get_scene_dts_generate_strategic() {
	init_settings();
	return BitField<SceneDTSGenerateStrategic>(GLOBAL_GET(kRtSceneDTSGenerateStrategic));
}

bool Settings::is_script_inline_resource_uid() {
	init_settings();
	return GLOBAL_GET(kScriptInlineResourceUID);
}

uint16_t Settings::get_debugger_port() {
	static uint16_t debugger_port_override = [] {
		// Check for --js-debugger-port <port> command line argument.
		const PackedStringArray &cmdline_args = OS::get_singleton()->get_cmdline_args();
		for (int i = 0; i < cmdline_args.size() - 1; i++) {
			if (cmdline_args[i] == "--js-debugger-port") {
				const String port_text = cmdline_args[i + 1];
				if (!port_text.is_empty() && port_text.is_valid_int()) {
					uint16_t port = port_text.to_int();
					jsb_notice(port > 0, "Found \"--js-debugger-port\" argument, debugger will start on port %d", port);
					return port;
				}
				break;
			}
		}
		return (uint16_t)0;
	}();

	if (debugger_port_override != 0) return debugger_port_override;
#ifdef TOOLS_ENABLED
	if (editor_settings_available()) {
		init_editor_settings();
		return EDITOR_GET(kEdDebuggerPort);
	} else {
		return 0; // 确保使用 0 无法启动调试功能
	}
#endif
	init_settings();
	return GLOBAL_GET(kRtDebuggerPort);
}

String Settings::get_debugger_source_map_base_url() {
	init_settings();
	return GLOBAL_GET(kRtDebuggerSourceMapBaseUrl);
}

bool Settings::get_wait_for_debugger() {
	init_settings();
	return GLOBAL_GET(kRtDebuggerWaitForDebugger);
}

bool Settings::get_sourcemap_enabled() {
	init_settings();
	return GLOBAL_GET(kRtSourceMapEnabled);
}

String Settings::get_project_data_dir_name() {
	bool use_hidden_directory = GLOBAL_GET("application/config/use_hidden_project_data_directory");
	return use_hidden_directory ? ".godot" : "godot";
}

String Settings::get_jsb_out_dir_name() {
	return get_project_data_dir_name().path_join(JSB_MODULE_NAME_STRING);
}

String Settings::get_tsbuildinfo_path() {
	return get_project_data_dir_name().path_join(".tsbuildinfo");
}

String Settings::get_jsb_out_res_path() {
	return "res://" + get_jsb_out_dir_name();
}

PackedStringArray Settings::get_additional_search_paths() {
	init_settings();
	return GLOBAL_GET(kRtAdditionalSearchPaths);
}

String Settings::get_entry_script_path() {
	init_settings();
	const String path = GLOBAL_GET(kRtEntryScriptPath);
	return ResourceUID::ensure_path(path);
}

bool Settings::get_camel_case_bindings_enabled() {
	init_settings();
	return GLOBAL_GET(kRtCamelCaseBindingsEnabled);
}

void Settings::set_ignored_classes(const PackedStringArray &p_ignored_classes) {
	init_settings();
	ProjectSettings::get_singleton()->set_setting(kRtIgnoredClasses, p_ignored_classes);
	ProjectSettings::get_singleton()->save();
}

PackedStringArray Settings::get_ignored_classes() {
	init_settings();
	return GLOBAL_GET(kRtIgnoredClasses);
}

String Settings::get_indentation() {
#ifdef TOOLS_ENABLED
	if (Engine::get_singleton()->is_editor_hint()) {
		init_settings();
		// use_space_indentation
		if (!!EDITOR_GET("text_editor/behavior/indent/type")) {
			const int indent_size = EDITOR_GET("text_editor/behavior/indent/size");
			return String(" ").repeat(indent_size);
		}
	}
#endif
	return "\t";
}

} //namespace jsb::internal
