/************************************************************************/
/*  jsb_bridge_table.cpp                                                */
/************************************************************************/
/*  This file is part of:                                               */
/*                                GodotJS-Ext                           */
/*              https://github.com/Daylily-Zeleen/GodotJS-Ext           */
/*                                                                      */
/*  Copyright (c) 2026-present 忘忧の (Daylily-Zeleen)                  */
/*                 - Contact: daylily-zeleen@foxmail.com                */
/*  Copyright (c) Contributors of GodotJS                               */
/*                 - <https://github.com/godotjs/GodotJS>               */
/*                                                                      */
/*  This library is free software; you can redistribute it and/or       */
/*  modify it under the terms of the GNU Lesser General Public          */
/*  License as published by the Free Software Foundation; either        */
/*  version 2.1 of the License, or (at your option) any later version.  */
/*                                                                      */
/*  This library is distributed in the hope that it will be useful,     */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of      */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU   */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#include "jsb_bridge_table.h"
#include <cstdio>

#include "../bridge/jsb_environment.h"
#include "../bridge/jsb_type_convert.h"
#include "../weaver/jsb_script_language.h"
#include "../impl/shared/jsb_statistics.h"
#include <godot_cpp/classes/engine.hpp>

namespace jsb {

// ---------------------------------------------------------------------------
// Bridge implementations. Every function runs on the main thread inside the
// runtime extension; results are constructed through THIS extension's
// godot-cpp into the caller-owned variant storage.
// ---------------------------------------------------------------------------

static void bridge_eval(const char *p_source_utf8, int64_t p_length,
                        GDExtensionVariantPtr r_result_variant, int64_t *r_error) {
	if (!r_error) return;
	*r_error = ERR_UNAVAILABLE;

	GodotJSScriptLanguage *lang = GodotJSScriptLanguage::get_singleton();
	if (lang == nullptr || !lang->is_initialized()) {
		*r_error = ERR_UNCONFIGURED;
		return;
	}
	if (!Thread::is_main_thread()) {
		*r_error = ERR_UNAVAILABLE;
		return;
	}

	Error err = OK;
	jsb::JSValueMove result = lang->eval_source(String::utf8(p_source_utf8, (int)p_length), err);
	if (err != OK) {
		*r_error = err;
		return;
	}

	Variant value = result.to_variant();
	if (r_result_variant) {
		::godot::gdextension_interface::variant_new_copy(r_result_variant, value._native_ptr());
	}
	*r_error = OK;
}

static void bridge_eval_with_arg(const char *p_source_utf8, int64_t p_length,
                                 GDExtensionConstVariantPtr p_argument_variant,
                                 GDExtensionVariantPtr r_result_variant, int64_t *r_error) {
	fprintf(stderr, "[BRIDGE-DBG] eval_with_arg enter\n");
	if (!r_error) return;
	*r_error = ERR_UNAVAILABLE;

	GodotJSScriptLanguage *lang = GodotJSScriptLanguage::get_singleton();
	if (lang == nullptr || !lang->is_initialized()) {
		*r_error = ERR_UNCONFIGURED;
		return;
	}
	if (!Thread::is_main_thread()) {
		*r_error = ERR_UNAVAILABLE;
		return;
	}

	std::shared_ptr<jsb::Environment> env = lang->get_environment();
	if (!env) {
		*r_error = ERR_UNCONFIGURED;
		return;
	}

	v8::Isolate *isolate = env->get_isolate();
	JSB_ISOLATE_SCOPE(isolate);
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = env->get_context();
	v8::Context::Scope context_scope(context);

	// expose the argument as a transient global `__jsb_arg`
	Variant arg;
	if (p_argument_variant) {
		arg = Variant(p_argument_variant);
	}
	v8::Local<v8::Value> arg_value;
	bool conv_ok = jsb::TypeConvert::gd_var_to_js(isolate, context, arg, arg_value);
	fprintf(stderr, "[BRIDGE-DBG] gd_var_to_js ok=%d type=%d\n", (int)conv_ok, (int)arg.get_type());
	if (!conv_ok) {
		*r_error = ERR_INVALID_PARAMETER;
		return;
	}
	const v8::Local<v8::String> arg_key = impl::Helper::new_string_ascii(isolate, "__jsb_arg");
	context->Global()->Set(context, arg_key, arg_value).Check();

	Error err = OK;
	const String src_str = String::utf8(p_source_utf8, (int)p_length);
	const CharString src_utf8 = src_str.utf8();
	fprintf(stderr, "[BRIDGE-DBG] before eval (%d chars)\n", (int)src_utf8.length());
	jsb::JSValueMove result = env->eval_source(src_utf8.get_data(), src_utf8.length(), "bridge_eval", err);
	fprintf(stderr, "[BRIDGE-DBG] after eval err=%d\n", (int)err);
	if (err != OK) {
		*r_error = err;
		return;
	}

	Variant value = result.to_variant();
	fprintf(stderr, "[BRIDGE-DBG] to_variant done type=%d\n", (int)value.get_type());
	if (r_result_variant) {
		::godot::gdextension_interface::variant_new_copy(r_result_variant, value._native_ptr());
	}
	*r_error = OK;
	fprintf(stderr, "[BRIDGE-DBG] eval_with_arg done\n");
}

static void bridge_get_module_source_info(const char *p_path_utf8, int64_t p_length,
                                          GDExtensionVariantPtr r_result_variant, int64_t *r_error) {
	if (!r_error) return;
	*r_error = ERR_UNAVAILABLE;

	GodotJSScriptLanguage *lang = GodotJSScriptLanguage::get_singleton();
	if (lang == nullptr || !lang->is_initialized() || !Thread::is_main_thread()) {
		*r_error = ERR_UNCONFIGURED;
		return;
	}
	if (!r_result_variant) {
		*r_error = ERR_INVALID_PARAMETER;
		return;
	}

	const String path = String::utf8(p_path_utf8, (int)p_length);
	Dictionary info;

	std::shared_ptr<jsb::Environment> env = lang->get_environment();
	jsb::JavaScriptModule *module = nullptr;
	if (env && env->load(path, &module) == OK && module != nullptr) {
		info["source"] = module->source_info.source_filepath;
		info["package"] = module->source_info.package_filepath;
		*r_error = OK;
	} else {
		*r_error = ERR_CANT_OPEN;
	}

	Variant value = info;
	::godot::gdextension_interface::variant_new_copy(r_result_variant, value._native_ptr());
}

static void bridge_get_module_direct_dependencies(const char *p_path_utf8, int64_t p_length,
                                                  GDExtensionVariantPtr r_result_variant, int64_t *r_error) {
	if (!r_error) return;
	*r_error = ERR_UNAVAILABLE;

	GodotJSScriptLanguage *lang = GodotJSScriptLanguage::get_singleton();
	if (lang == nullptr || !lang->is_initialized() || !Thread::is_main_thread()) {
		*r_error = ERR_UNCONFIGURED;
		return;
	}
	if (!r_result_variant) {
		*r_error = ERR_INVALID_PARAMETER;
		return;
	}

	const String path = String::utf8(p_path_utf8, (int)p_length);
	PackedStringArray deps;

	std::shared_ptr<jsb::Environment> env = lang->get_environment();
	jsb::JavaScriptModule *module = nullptr;
	if (env && env->load(path, &module) == OK && module != nullptr) {
		v8::Isolate *isolate = env->get_isolate();
		v8::HandleScope handle_scope(isolate);
		const v8::Local<v8::Context> context = env->get_context();

		if (!module->module.IsEmpty()) {
			v8::Local<v8::Value> temp;
			if (module->module.Get(isolate).As<v8::Object>()
					->Get(context, jsb_name(env, children)).ToLocal(&temp)
				&& temp->IsArray()) {
				const v8::Local<v8::Array> children = temp.As<v8::Array>();
				const int32_t len = children->Length();
				for (int32_t i = 0; i < len; i++) {
					if (children->Get(context, i).ToLocal(&temp) && temp->IsObject()) {
						if (temp.As<v8::Object>()->Get(context, jsb_name(env, filename)).ToLocal(&temp)) {
							String filename = jsb::impl::Helper::to_string(isolate, temp);
							if (!filename.is_empty()) {
								deps.push_back(filename);
							}
						}
					}
				}
			}
		}
		*r_error = OK;
	} else {
		*r_error = ERR_CANT_OPEN;
	}

	Variant value = deps;
	::godot::gdextension_interface::variant_new_copy(r_result_variant, value._native_ptr());
}

static void bridge_fill_statistics(void *p_statistics_raw, int64_t *r_error) {
	if (!r_error) return;
	*r_error = ERR_UNAVAILABLE;

	if (!p_statistics_raw) {
		*r_error = ERR_INVALID_PARAMETER;
		return;
	}
	GodotJSScriptLanguage *lang = GodotJSScriptLanguage::get_singleton();
	if (lang == nullptr || !lang->is_initialized() || !Thread::is_main_thread()) {
		*r_error = ERR_UNCONFIGURED;
		return;
	}

	std::shared_ptr<jsb::Environment> env = lang->get_environment();
	if (!env) {
		*r_error = ERR_UNCONFIGURED;
		return;
	}
	env->get_statistics(*static_cast<jsb::Statistics *>(p_statistics_raw));
	*r_error = OK;
}

static void bridge_scan_external_changes(int64_t *r_error) {
	if (!r_error) return;
	*r_error = ERR_UNAVAILABLE;

	GodotJSScriptLanguage *lang = GodotJSScriptLanguage::get_singleton();
	if (lang == nullptr || !lang->is_initialized() || !Thread::is_main_thread()) {
		*r_error = ERR_UNCONFIGURED;
		return;
	}
	lang->scan_external_changes();
	*r_error = OK;
}

static void bridge_is_global_class_generic(const char *p_path_utf8, int64_t p_length,
                                           GDExtensionVariantPtr r_result_variant, int64_t *r_error) {
	if (!r_error) return;
	*r_error = ERR_UNAVAILABLE;

	GodotJSScriptLanguage *lang = GodotJSScriptLanguage::get_singleton();
	if (lang == nullptr || !lang->is_initialized() || !Thread::is_main_thread()) {
		*r_error = ERR_UNCONFIGURED;
		return;
	}
	if (!r_result_variant) {
		*r_error = ERR_INVALID_PARAMETER;
		return;
	}

	const String path = String::utf8(p_path_utf8, (int)p_length);
	Variant value = lang->is_global_class_generic(path);
	::godot::gdextension_interface::variant_new_copy(r_result_variant, value._native_ptr());
	*r_error = OK;
}

static void bridge_request_gc(int64_t *r_error) {
	if (!r_error) return;
	*r_error = ERR_UNAVAILABLE;

	if (!Thread::is_main_thread()) {
		*r_error = ERR_UNAVAILABLE;
		return;
	}
	jsb::Environment::gc();
	*r_error = OK;
}

static void bridge_get_reserved_words(const char *p_arg_utf8, int64_t p_length,
                                      GDExtensionVariantPtr r_result_variant, int64_t *r_error) {
	if (!r_error) return;
	*r_error = ERR_UNAVAILABLE;

	GodotJSScriptLanguage *lang = GodotJSScriptLanguage::get_singleton();
	if (lang == nullptr || !Thread::is_main_thread()) {
		*r_error = ERR_UNCONFIGURED;
		return;
	}
	if (!r_result_variant) {
		*r_error = ERR_INVALID_PARAMETER;
		return;
	}

	Variant value = lang->_get_reserved_words();
	::godot::gdextension_interface::variant_new_copy(r_result_variant, value._native_ptr());
	*r_error = OK;
}

static JsbBridgeTable g_bridge_table = {
	sizeof(JsbBridgeTable),
	&bridge_eval,
	&bridge_eval_with_arg,
	&bridge_get_module_source_info,
	&bridge_get_module_direct_dependencies,
	&bridge_fill_statistics,
	&bridge_scan_external_changes,
	&bridge_is_global_class_generic,
	&bridge_request_gc,
	&bridge_get_reserved_words,
};

const JsbBridgeTable *get_bridge_table() {
	return &g_bridge_table;
}

} //namespace jsb
