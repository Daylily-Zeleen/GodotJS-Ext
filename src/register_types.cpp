#include "register_types.h"

#include "api_tool/api_tool.h"
#include "compat/jsb_compat.h"
#include "weaver/jsb_weaver.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>

#ifdef TOOLS_ENABLED
#	include "weaver-editor/jsb_weaver_editor.h"
#	include <godot_cpp/variant/callable_method_pointer.hpp>
#	include "api_tool/editor/api_tool_editor.h"
#endif // TOOLS_ENABLED

#ifdef JSB_TESTS_ENABLED
#	include "doctest/doctest.h"
#	include <godot_cpp/classes/window.hpp>
#	include <godot_cpp/classes/scene_tree.hpp>
#endif

static Ref<ResourceFormatLoaderGodotJSScript> resource_loader_js;
static Ref<ResourceFormatSaverGodotJSScript> resource_saver_js;

void jsb_initialize_module(ModuleInitializationLevel p_level) {
	switch (p_level) {
		case MODULE_INITIALIZATION_LEVEL_SERVERS: {
			GDREGISTER_CLASS(GodotJSScript);
#ifdef TOOLS_ENABLED
			GDREGISTER_CLASS(GodotJSEditorHelper);
#endif // TOOLS_ENABLED
			GDREGISTER_INTERNAL_CLASS(GodotJSScriptLanguage);

			// register javascript language
			GodotJSScriptLanguage *script_language_js = memnew(GodotJSScriptLanguage());
			Engine::get_singleton()->register_script_language(script_language_js);
		} break;
		case MODULE_INITIALIZATION_LEVEL_SCENE: {
			// SERVERS 阶段时部分单例还未被注册，无法获取 ResourceLoader 和 ResourceSaver 单例
			GDREGISTER_INTERNAL_CLASS(ResourceFormatLoaderGodotJSScript);
			resource_loader_js.instantiate();
			ResourceLoader::get_singleton()->add_resource_format_loader(resource_loader_js);

			GDREGISTER_INTERNAL_CLASS(ResourceFormatSaverGodotJSScript);
			resource_saver_js.instantiate();
			ResourceSaver::get_singleton()->add_resource_format_saver(resource_saver_js);
		} break;
#ifdef TOOLS_ENABLED
		case MODULE_INITIALIZATION_LEVEL_EDITOR: {
			GDREGISTER_INTERNAL_CLASS(GodotJSExportPlugin);
			GDREGISTER_INTERNAL_CLASS(GodotJSEditorPlugin);
			EditorPlugins::add_by_type<GodotJSEditorPlugin>();
		}
#endif // TOOLS_ENABLED
		default:
			break;
	}
}

void jsb_uninitialize_module(ModuleInitializationLevel p_level) {
	switch (p_level) {
		case MODULE_INITIALIZATION_LEVEL_SERVERS: {
			GodotJSScriptLanguage *script_language_js = GodotJSScriptLanguage::get_singleton();
			jsb_check(script_language_js);
			Engine::get_singleton()->unregister_script_language(script_language_js);
			memdelete(script_language_js);
		} break;
		case MODULE_INITIALIZATION_LEVEL_SCENE: {
			jsb_check(resource_loader_js.is_valid());
			ResourceLoader::get_singleton()->remove_resource_format_loader(resource_loader_js);
			resource_loader_js.unref();

			jsb_check(resource_saver_js.is_valid());
			ResourceSaver::get_singleton()->remove_resource_format_saver(resource_saver_js);
			resource_saver_js.unref();
		} break;
#ifdef TOOLS_ENABLED
		case MODULE_INITIALIZATION_LEVEL_EDITOR: {
			EditorPlugins::remove_by_type<GodotJSEditorPlugin>();
		}
#endif // TOOLS_ENABLED
		default:
			break;
	}
}

void jsb_startup() {
#ifdef TOOLS_ENABLED
	// Check for --godotjs-api-generate <path> command line argument.
	// This is passed by full_generate_and_reboot() when relaunching the editor
	// to trigger API data regeneration.
	const PackedStringArray &cmdline_args = OS::get_singleton()->get_cmdline_args();
	for (int i = 0; i < cmdline_args.size() - 1; i++) {
		if (cmdline_args[i] == "--godotjs-api-generate") {
			const String extension_api_path = cmdline_args[i + 1];
			if (!extension_api_path.is_empty()) {
				api_tool::generate_api_tool_data(extension_api_path);
			}
			return;
		}
	}
#endif // TOOLS_ENABLED
	if (api_tool::has_generated_data()) {
		api_tool::initialize();
	}

#ifdef TOOLS_ENABLED
	if (Engine::get_singleton()->is_editor_hint()) {
		std::shared_ptr<jsb::Environment> env = GodotJSScriptLanguage::get_singleton()->get_environment();
		if (!env->is_debugger_started()) {
			uint16_t debugger_port = jsb::internal::Settings::get_debugger_port();
			if (debugger_port != 0) {
				env->start_debugger(debugger_port);
			}
		}
	}
#endif // TOOLS_ENABLED

#ifdef JSB_TESTS_ENABLED
	// Run doctest unit tests if --jsb-run-tests is passed on command line
	{
		const PackedStringArray &args = OS::get_singleton()->get_cmdline_args();
		bool run_tests = false;
		for (int i = 0; i < args.size(); i++) {
			if (args[i] == "--jsb-run-tests") {
				run_tests = true;
				break;
			}
		}
		if (run_tests) {
			doctest::Context context;
			// doctest defaults to order-by=file (sorted by __FILE__), which causes
			// cross-platform execution-order differences: on GCC/Linux the .cpp
			// file sorts before the .h files, so "[jsb] Finalize tests" (registered
			// in jsb_test_main.cpp) runs FIRST and its _init() early-returns without
			// rebuilding the environment. Subsequent initer tests destroy the
			// environment, and when the main loop runs its first _frame() after
			// quit(), environment_ is null → SIGSEGV. On MSVC/Windows the __FILE__
			// path form differs, reversing the sort order so Finalize runs LAST
			// and rebuilds the environment — which is why it works locally but
			// crashes on CI. Setting order-by=none makes doctest use registration
			// order (include order in jsb_test_main.cpp), guaranteeing Finalize
			// runs last on every platform.
			context.setOption("order-by", "none");
			int exit_code = context.run();
			// After doctest finishes, initer test destructors may have destroyed
			// the JS environment (environment_ = null). The main loop will call
			// _frame() → environment_->update() on the first iteration after
			// quit(), causing a null-pointer SIGSEGV. Force-rebuild the environment
			// here so _frame() is safe. _init() is idempotent (guarded by
			// once_initialized_), but after the last initer's _finish() the flag
			// is false, so this will actually rebuild.
			GodotJSScriptLanguage::get_singleton()->_init();
			if (SceneTree *scene_tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop())) {
				scene_tree->quit(exit_code);
			} else {
				std::exit(exit_code);
			}
		}
	}
#endif // JSB_TESTS_ENABLED
}

void jsb_shutdown() {
	api_tool::finalize();
}

extern "C" {
GDExtensionBool GDE_EXPORT jsb_gdextension_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

	init_obj.register_startup_callback(jsb_startup);
	init_obj.register_initializer(jsb_initialize_module);
	init_obj.register_terminator(jsb_uninitialize_module);
	init_obj.register_shutdown_callback(jsb_shutdown);
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_CORE);

	return init_obj.init();
}
}
