/************************************************************************/
/*  register_types.cpp                                                  */
/************************************************************************/
/*  This file is part of:                                               */
/*                                GodotJS-Ext                           */
/*              https://github.com/Daylily-Zeleen/GodotJS-Ext           */
/*                                                                      */
/*  Copyright (c) 2026-present 忘忧の (Daylily-Zeleen)                  */
/*                 - Contact: daylily-zeleen@foxmail.com                */
/*                                                                      */
/*  This library is free software; you can redistribute it and/or       */
/*  modify it under the terms of the GNU Lesser General Public          */
/*  License as published by the Free Software Foundation; either        */
/*  version 2.1 of the License, or (at your option) any later version.  */
/*                                                                      */
/*  This library is distributed in the hope that it will be useful,     */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of      */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU   */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#include "register_types.h"

#ifdef JSB_TESTS_ENABLED
#	include "../tests/jsb_test_runner.h"
#endif
#include "api_tool/api_tool.h"
#include "weaver/jsb_weaver.h"
#include <compat/jsb_compat.h>
#include <internal/jsb_runtime_settings.h>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

static Ref<ResourceFormatLoaderGodotJSScript> resource_loader_js;
static Ref<ResourceFormatSaverGodotJSScript> resource_saver_js;

void jsb_initialize_module(ModuleInitializationLevel p_level) {
	switch (p_level) {
		case MODULE_INITIALIZATION_LEVEL_SERVERS: {
#ifdef JSB_TESTS_ENABLED
			Engine::get_singleton()->set_meta(jsb::tests::RUNTIME_TEST_FLAG, false);
#endif
			// Register runtime-owned project settings before anything reads them.
			// (SERVERS is the lowest level the engine passes to GDExtensions.)
			jsb::internal::settings::init_runtime_settings();

			GDREGISTER_CLASS(GodotJSScript);
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
		default:
			break;
	}
}

void jsb_startup() {
	UtilityFunctions::print("[jsb] startup");
	if (api_tool::has_generated_data()) {
		api_tool::initialize();
	}

#ifdef JSB_TESTS_ENABLED
	jsb::tests::try_run(jsb::tests::RUNTIME_TEST_FLAG);
#endif // JSB_TESTS_ENABLED
}

void jsb_shutdown() {
	UtilityFunctions::print("[jsb] shutdown: finalizing api_tool");
	api_tool::finalize();
	UtilityFunctions::print("[jsb] shutdown: api_tool finalized");
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
