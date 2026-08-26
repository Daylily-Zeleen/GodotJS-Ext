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

static godot::Error bridge_eval(const char *p_source_utf8, int64_t p_length,
                                GDExtensionVariantPtr r_result_variant) {
	GodotJSScriptLanguage *lang = GodotJSScriptLanguage::get_singleton();
	if (lang == nullptr || !lang->is_initialized()) {
		return ERR_UNCONFIGURED;
	}
	if (!Thread::is_main_thread()) {
		return ERR_UNAVAILABLE;
	}

	Error err = OK;
	jsb::JSValueMove result = lang->eval_source(String::utf8(p_source_utf8, (int)p_length), err);
	if (err != OK) {
		return err;
	}

	Variant value = result.to_variant();
	if (r_result_variant) {
		::godot::gdextension_interface::variant_new_copy(r_result_variant, value._native_ptr());
	}
	return OK;
}

static godot::Error bridge_eval_with_arg(const char *p_source_utf8, int64_t p_length,
                                 GDExtensionConstVariantPtr p_argument_variant,
                                 GDExtensionVariantPtr r_result_variant) {
	GodotJSScriptLanguage *lang = GodotJSScriptLanguage::get_singleton();
	if (lang == nullptr || !lang->is_initialized()) {
		return ERR_UNCONFIGURED;
	}
	if (!Thread::is_main_thread()) {
		return ERR_UNAVAILABLE;
	}

	std::shared_ptr<jsb::Environment> env = lang->get_environment();
	if (!env) {
		return ERR_UNCONFIGURED;
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
	if (!jsb::TypeConvert::gd_var_to_js(isolate, context, arg, arg_value)) {
		return ERR_INVALID_PARAMETER;
	}
	const v8::Local<v8::String> arg_key = impl::Helper::new_string_ascii(isolate, "__jsb_arg");
	context->Global()->Set(context, arg_key, arg_value).Check();

	Error err = OK;
	const String src_str = String::utf8(p_source_utf8, (int)p_length);
	const CharString src_utf8 = src_str.utf8();
	jsb::JSValueMove result = env->eval_source(src_utf8.get_data(), src_utf8.length(), "bridge_eval", err);
	if (err != OK) {
		return err;
	}

	Variant value = result.to_variant();
	if (r_result_variant) {
		::godot::gdextension_interface::variant_new_copy(r_result_variant, value._native_ptr());
	}
	return OK;
}

static godot::Error bridge_get_module_source_info(const char *p_path_utf8, int64_t p_length,
                                      GDExtensionVariantPtr r_result_variant) {
	GodotJSScriptLanguage *lang = GodotJSScriptLanguage::get_singleton();
	if (lang == nullptr || !lang->is_initialized() || !Thread::is_main_thread()) {
		return ERR_UNCONFIGURED;
	}
	if (!r_result_variant) {
		return ERR_INVALID_PARAMETER;
	}

	const String path = String::utf8(p_path_utf8, (int)p_length);
	Dictionary info;

	std::shared_ptr<jsb::Environment> env = lang->get_environment();
	jsb::JavaScriptModule *module = nullptr;
	if (env && env->load(path, &module) == OK && module != nullptr) {
		info["source"] = module->source_info.source_filepath;
		info["package"] = module->source_info.package_filepath;
	} else {
		return ERR_CANT_OPEN;
	}

	Variant value = info;
	::godot::gdextension_interface::variant_new_copy(r_result_variant, value._native_ptr());
	return OK;
}

static godot::Error bridge_get_module_direct_dependencies(const char *p_path_utf8, int64_t p_length,
                                      GDExtensionVariantPtr r_result_variant) {
	GodotJSScriptLanguage *lang = GodotJSScriptLanguage::get_singleton();
	if (lang == nullptr || !lang->is_initialized() || !Thread::is_main_thread()) {
		return ERR_UNCONFIGURED;
	}
	if (!r_result_variant) {
		return ERR_INVALID_PARAMETER;
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
	} else {
		return ERR_CANT_OPEN;
	}

	Variant value = deps;
	::godot::gdextension_interface::variant_new_copy(r_result_variant, value._native_ptr());
	return OK;
}

static godot::Error bridge_fill_statistics(void *p_statistics_raw) {
	if (!p_statistics_raw) {
		return ERR_INVALID_PARAMETER;
	}
	GodotJSScriptLanguage *lang = GodotJSScriptLanguage::get_singleton();
	if (lang == nullptr || !lang->is_initialized() || !Thread::is_main_thread()) {
		return ERR_UNCONFIGURED;
	}

	std::shared_ptr<jsb::Environment> env = lang->get_environment();
	if (!env) {
		return ERR_UNCONFIGURED;
	}
	env->get_statistics(*static_cast<jsb::Statistics *>(p_statistics_raw));
	return OK;
}

static godot::Error bridge_scan_external_changes() {
	GodotJSScriptLanguage *lang = GodotJSScriptLanguage::get_singleton();
	if (lang == nullptr || !lang->is_initialized() || !Thread::is_main_thread()) {
		return ERR_UNCONFIGURED;
	}
	lang->scan_external_changes();
	return OK;
}

static godot::Error bridge_is_global_class_generic(const char *p_path_utf8, int64_t p_length,
                                      GDExtensionVariantPtr r_result_variant) {
	GodotJSScriptLanguage *lang = GodotJSScriptLanguage::get_singleton();
	if (lang == nullptr || !lang->is_initialized() || !Thread::is_main_thread()) {
		return ERR_UNCONFIGURED;
	}
	if (!r_result_variant) {
		return ERR_INVALID_PARAMETER;
	}

	const String path = String::utf8(p_path_utf8, (int)p_length);
	Variant value = lang->is_global_class_generic(path);
	::godot::gdextension_interface::variant_new_copy(r_result_variant, value._native_ptr());
	return OK;
}

static godot::Error bridge_request_gc() {
	if (!Thread::is_main_thread()) {
		return ERR_UNAVAILABLE;
	}
	jsb::Environment::gc();
	return OK;
}


namespace {
// Trampoline that adapts an editor-side write callback into the runtime's
// IConsoleOutput list. Lifetime is owned by the bridge until removal.
class EditorConsoleTrampoline final : public jsb::internal::IConsoleOutput {
public:
	void *userdata = nullptr;
	void (*write_fn)(void *, int32_t, const char *, int64_t) = nullptr;

	void write(jsb::internal::ELogSeverity::Type p_severity, const String &p_text) override {
		const CharString utf8 = p_text.utf8();
		write_fn(userdata, (int32_t)p_severity, utf8.get_data(), utf8.length());
	}
};

Vector<EditorConsoleTrampoline *> g_editor_consoles;
int64_t g_next_console_handle = 1;
} //namespace

static int64_t bridge_add_console_output(void *p_userdata,
        void (*p_write)(void *p_userdata, int32_t p_severity, const char *p_text_utf8, int64_t p_length)) {
	if (!p_write) return -1;
	EditorConsoleTrampoline *t = memnew(EditorConsoleTrampoline);
	t->userdata = p_userdata;
	t->write_fn = p_write;
	g_editor_consoles.append(t);
	return (int64_t)(intptr_t)t;
}

static godot::Error bridge_remove_console_output(int64_t p_handle) {
	EditorConsoleTrampoline *t = (EditorConsoleTrampoline *)(intptr_t)p_handle;
	const int idx = g_editor_consoles.find(t);
	if (idx >= 0) {
		memdelete(t);
		g_editor_consoles.remove_at(idx);
	}
	return OK; // idempotent
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
	&bridge_add_console_output,
	&bridge_remove_console_output,
};

const JsbBridgeTable *get_bridge_table() {
	return &g_bridge_table;
}

} //namespace jsb
