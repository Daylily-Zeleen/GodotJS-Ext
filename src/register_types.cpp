#include "register_types.h"

#include "api_tool/api_tool.h"
#include "weaver/jsb_weaver.h"
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/classes/os.hpp>

#ifdef TOOLS_ENABLED
#include "weaver-editor/jsb_weaver_editor.h"
#include <godot_cpp/variant/callable_method_pointer.hpp>
#endif // TOOLS_ENABLED

#ifdef JSB_TESTS_ENABLED
#include "doctest/doctest.h"
#endif

static Ref<ResourceFormatLoaderGodotJSScript> resource_loader_js;
static Ref<ResourceFormatSaverGodotJSScript> resource_saver_js;

void jsb_initialize_module(ModuleInitializationLevel p_level)
{
    if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) // TODO: MODULE_INITIALIZATION_LEVEL_SERVERS
    {
        GDREGISTER_CLASS(GodotJSScript);
#ifdef TOOLS_ENABLED
        GDREGISTER_CLASS(GodotJSEditorHelper);
#endif // TOOLS_ENABLED
        GDREGISTER_INTERNAL_CLASS(GodotJSScriptLanguage);

        jsb::impl::GlobalInitialize::init();

        // register javascript language
        GodotJSScriptLanguage* script_language_js = memnew(GodotJSScriptLanguage());
        Engine::get_singleton()->register_script_language(script_language_js);

        GDREGISTER_INTERNAL_CLASS(ResourceFormatLoaderGodotJSScript);
        resource_loader_js.instantiate();
        ResourceLoader::get_singleton()->add_resource_format_loader(resource_loader_js);
        
        GDREGISTER_INTERNAL_CLASS(ResourceFormatSaverGodotJSScript);
        resource_saver_js.instantiate();
        ResourceSaver::get_singleton()->add_resource_format_saver(resource_saver_js);
    }
#ifdef TOOLS_ENABLED
    if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR)
    {
        GDREGISTER_INTERNAL_CLASS(GodotJSExportPlugin);
        GDREGISTER_INTERNAL_CLASS(GodotJSEditorPlugin);
        EditorPlugins::add_by_type<GodotJSEditorPlugin>();
    }
#endif
}

void jsb_uninitialize_module(ModuleInitializationLevel p_level)
{
    if (p_level == MODULE_INITIALIZATION_LEVEL_CORE)
    {
        ResourceLoader::get_singleton()->remove_resource_format_loader(resource_loader_js);
        resource_loader_js.unref();

        ResourceSaver::get_singleton()->remove_resource_format_saver(resource_saver_js);
        resource_saver_js.unref();

        GodotJSScriptLanguage *script_language_js = GodotJSScriptLanguage::get_singleton();
        jsb_check(script_language_js);
        Engine::get_singleton()->unregister_script_language(script_language_js);
        memdelete(script_language_js);
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
        if (!env->is_debugger_started())
        {
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
            std::exit(context.run());
        }
    }
#endif // JSB_TESTS_ENABLED
}

extern "C"
{
    GDExtensionBool GDE_EXPORT jsb_gdextension_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, GDExtensionClassLibraryPtr p_library, GDExtensionInitialization* r_initialization)
    {
        GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

        init_obj.register_startup_callback(jsb_startup);
        init_obj.register_initializer(jsb_initialize_module);
        init_obj.register_terminator(jsb_uninitialize_module);
        init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_CORE);

        return init_obj.init();
    }
}
