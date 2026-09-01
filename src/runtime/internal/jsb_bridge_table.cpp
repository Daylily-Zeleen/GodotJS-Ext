/************************************************************************/
/*  jsb_bridge_table.cpp                                                */
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

#include "jsb_bridge_table.h"
#include <cstdio>

#include "../bridge/jsb_environment.h"
#include "../bridge/jsb_type_convert.h"
#include "../weaver/jsb_script_language.h"
#include <internal/jsb_statistics.h>
#include <godot_cpp/classes/engine.hpp>

#if JSB_WITH_NODE
#	include "../bridge/jsb_bridge_helper.h"
#	include "../impl/node/jsb_node.h"
#	include <internal/jsb_console_output.h>
#	include <string_builder.h>
#endif

namespace jsb {

// ---------------------------------------------------------------------------
// Bridge implementations. Every function runs on the main thread inside the
// runtime extension; results are constructed through THIS extension's
// godot-cpp into the caller-owned variant storage.
// ---------------------------------------------------------------------------

static godot::Error bridge_eval(const char *p_source_utf8, int64_t p_length, GDExtensionVariantPtr r_result_variant) {
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

static godot::Error bridge_eval_with_arg(const char *p_source_utf8, int64_t p_length, GDExtensionConstVariantPtr p_argument_variant, GDExtensionVariantPtr r_result_variant) {
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

static godot::Error bridge_get_module_source_info(const char *p_path_utf8, int64_t p_length, GDExtensionVariantPtr r_result_variant) {
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

static godot::Error bridge_get_module_direct_dependencies(const char *p_path_utf8, int64_t p_length, GDExtensionVariantPtr r_result_variant) {
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
			if (module->module.Get(isolate).As<v8::Object>()->Get(context, jsb_name(env, children)).ToLocal(&temp)
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

static godot::Error bridge_is_global_class_generic(const char *p_path_utf8, int64_t p_length, GDExtensionVariantPtr r_result_variant) {
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
} //namespace

#if JSB_WITH_NODE
// ---------------------------------------------------------------------------
// node console hook.
//
// The node bootstrap installs its own `console` object on the global, whose
// output goes to the node stdout and never reaches the editor console sinks
// (registered through `bridge_add_console_output`). This hook wraps the 9
// console methods so every call is *mirrored* into
// `internal::IConsoleOutput::internal_write` (reaching all registered sinks)
// and then *forwarded* to the original node implementation.
//
// Activation is a process-wide one-shot: the first `bridge_add_console_output`
// call arms the hook and installs it on every live Environment; afterwards
// every newly created Environment installs the hook by itself (one line at
// the end of NodeRuntime's constructor). The hook is never uninstalled --
// once the editor console capability showed up in this process, every
// Environment keeps the wrapped console.
//
// Implementation notes:
// - The original node function is attached to each wrapper as its V8 `data`
//   payload (`info.Data()`), so no per-environment C++ state is needed.
// - `console.assert(cond, ...)`: silent when `cond` is truthy (mirrors node
//   semantics -- neither mirrored nor forwarded); mirrored + forwarded only
//   when the assertion fails.
// - `console.time/timeEnd`: fully taken over with the same JSTimerTags logic
//   as the v8/qjs Essentials implementation (the elapsed value only exists
//   on the C++ side; the node-native timer writes to stdout only and cannot
//   reach the sinks). Not forwarded.
// ---------------------------------------------------------------------------

namespace {

static bool s_console_hook_active = false;

// mirror one line into the editor console sinks (IConsoleOutput list)
static void console_hook_write(internal::ELogSeverity::Type p_severity, const String &p_text) {
	internal::IConsoleOutput::internal_write(p_severity, p_text);
}

// join "[JS] arg1 arg2 ..." with the same BridgeHelper::stringify formatting
// as the Essentials console implementation
template <internal::ELogSeverity::Type Severity>
static void console_log_wrap(const v8::FunctionCallbackInfo<v8::Value> &info) {
	v8::Isolate *isolate = info.GetIsolate();

	StringBuilder sb;
	sb.append("[JS]");
	for (int index = 0; index < info.Length(); ++index) {
		if (String str = BridgeHelper::stringify(isolate, info[index]); str.length() > 0) {
			sb.append(" ");
			sb.append(str);
		}
	}
	console_hook_write(Severity, sb.as_string());

	// forward to the original node console function (attached as `data`)
	v8::Local<v8::Function> orig = info.Data().As<v8::Function>();
	v8::Local<v8::Value> argv[8];
	const int argc = info.Length() < 8 ? info.Length() : 8;
	for (int index = 0; index < argc; ++index) {
		argv[index] = info[index];
	}
	v8::Local<v8::Context> context = isolate->GetCurrentContext();
	jsb_unused(orig->Call(context, v8::Undefined(isolate), argc, argv));
}

// console.assert: node semantics -- silent when truthy, mirrored + forwarded
// only on failure (the node-native implementation keeps printing/throwing)
static void console_assert_wrap(const v8::FunctionCallbackInfo<v8::Value> &info) {
	v8::Isolate *isolate = info.GetIsolate();
	if (info.Length() > 0 && info[0]->BooleanValue(isolate)) {
		return;
	}

	StringBuilder sb;
	sb.append("[JS] Assertion failure:");
	for (int index = 1; index < info.Length(); ++index) {
		if (String str = BridgeHelper::stringify(isolate, info[index]); str.length() > 0) {
			sb.append(" ");
			sb.append(str);
		}
	}
	console_hook_write(internal::ELogSeverity::Assert, sb.as_string());

	v8::Local<v8::Function> orig = info.Data().As<v8::Function>();
	v8::Local<v8::Value> argv[8];
	const int argc = info.Length() > 0 ? (info.Length() - 1 < 8 ? info.Length() - 1 : 8) : 0;
	for (int index = 1; index < info.Length() && index - 1 < argc; ++index) {
		argv[index - 1] = info[index];
	}
	v8::Local<v8::Context> context = isolate->GetCurrentContext();
	jsb_unused(orig->Call(context, v8::Undefined(isolate), argc, argv));
}

// console.time/timeEnd: fully taken over (subject to revisit -- a better
// forwarding scheme may replace this). The tag table lives here in the bridge
// table TU (an editor-specific capability; NOT on Environment, which must stay
// engine-only in the node build), but keeps the Essentials semantics of one
// tag table per Environment: in node mode Environment and Isolate are 1:1
// (each Environment owns a NodeRuntime which creates its own isolate), so
// keying by isolate is exactly per-environment. Labels are compared as plain
// utf8 strings -- the same semantics as the native console.time label
// matching, without the isolate-bound TStrongRef<v8::String> bookkeeping.
static HashMap<v8::Isolate *, HashMap<String, uint64_t>> s_timer_tags;

// resolve the timer label from the first argument ('default' when undefined)
static String console_time_label(const v8::FunctionCallbackInfo<v8::Value> &info) {
	v8::Isolate *isolate = info.GetIsolate();
	return info[0]->IsUndefined() ? String("default") : impl::Helper::to_string(isolate, info[0]);
}

static void console_time_wrap(const v8::FunctionCallbackInfo<v8::Value> &info) {
	v8::Isolate *isolate = info.GetIsolate();
	if (!info[0]->IsUndefined() && !info[0]->IsString()) {
		jsb_throw(isolate, "bad argument");
		return;
	}
	const String label = console_time_label(info);
	HashMap<String, uint64_t> &tags = s_timer_tags[isolate];
	if (!tags.has(label)) {
		tags.insert(label, Time::get_singleton()->get_ticks_usec());
	} else {
		JSB_LOG(Warning, "timer tag '%s' already exists", label);
	}
}

static void console_time_end_wrap(const v8::FunctionCallbackInfo<v8::Value> &info) {
	v8::Isolate *isolate = info.GetIsolate();
	if (!info[0]->IsUndefined() && !info[0]->IsString()) {
		jsb_throw(isolate, "bad argument");
		return;
	}
	const uint64_t now = Time::get_singleton()->get_ticks_usec();
	const String label = console_time_label(info);
	HashMap<String, uint64_t> &tags = s_timer_tags[isolate];
	if (const uint64_t *start = tags.getptr(label)) {
		const uint64_t elapsed_ms = (now - *start) / 1000UL;
		tags.erase(label);
		JSB_LOG(Info, "%s: %dms - timer ended", label, (int64_t)elapsed_ms);
	} else {
		JSB_LOG(Warning, "timer tag '%s' not found", label);
	}
}

// install the wrapped console methods on one context. Scope contract: the
// caller must already hold the isolate (JSB_ISOLATE_SCOPE/Locker) AND a
// HandleScope -- `p_context` is a Local handle, so evaluating it without a
// HandleScope would crash (V8 requires a HandleScope to create locals).
static void console_hook_install_on_context(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context) {
	v8::Isolate *isolate = p_isolate;
	v8::Local<v8::Context> context = p_context;
	v8::Context::Scope context_scope(context);

	v8::Local<v8::Object> global = context->Global();
	v8::Local<v8::Value> console_val;
	if (!global->Get(context, impl::Helper::new_string(isolate, "console")).ToLocal(&console_val)
			|| !console_val->IsObject()) {
		return; // no console object (unexpected in node mode), nothing to hook
	}
	v8::Local<v8::Object> console = console_val.As<v8::Object>();

	struct MethodDef {
		const char *name;
		v8::FunctionCallback callback;
	};
	static constexpr MethodDef kMethods[] = {
		{ "log", console_log_wrap<internal::ELogSeverity::Log> },
		{ "info", console_log_wrap<internal::ELogSeverity::Info> },
		{ "debug", console_log_wrap<internal::ELogSeverity::Debug> },
		{ "warn", console_log_wrap<internal::ELogSeverity::Warning> },
		{ "error", console_log_wrap<internal::ELogSeverity::Error> },
		{ "trace", console_log_wrap<internal::ELogSeverity::Trace> },
		{ "assert", console_assert_wrap },
		{ "time", console_time_wrap },
		{ "timeEnd", console_time_end_wrap },
	};

	for (const MethodDef &def : kMethods) {
		v8::Local<v8::Value> orig_val;
		// keep the current (node-native) function as the wrapper's `data`
		// payload; if a previous wrapper is already installed this re-wraps
		// (harmless: output would be mirrored twice, but installation is
		// once-per-process by design)
		if (!console->Get(context, impl::Helper::new_string(isolate, def.name)).ToLocal(&orig_val)
				|| !orig_val->IsFunction()) {
			continue;
		}
		v8::Local<v8::Function> wrapper = impl::Helper::NewFunction(
				context, def.name, def.callback, orig_val);
		console->Set(context, impl::Helper::new_string(isolate, def.name), wrapper).Check();
	}
}

} //namespace

void bridge_console_hook_ensure(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context) {
	if (!s_console_hook_active) {
		return;
	}
	// called by NodeRuntime's constructor right after the bootstrap made the
	// console available on the given context; the caller holds the scope
	console_hook_install_on_context(p_isolate, p_context);
}

namespace {
// arm the hook process-wide and install it on every live environment
static void console_hook_activate() {
	if (s_console_hook_active) {
		return;
	}
	s_console_hook_active = true;
	const auto environments = Environment::get_all_environments();
	for (const auto &env : environments) {
		// skip environments that are about to be disposed or already disposed
		if (env->is_disposing()) continue;
		// enter the environment's isolate + HandleScope BEFORE evaluating
		// `get_context()` (a Local handle needs a HandleScope to be created)
		v8::Isolate *isolate = env->get_isolate();
		JSB_ISOLATE_SCOPE(isolate);
		v8::HandleScope handle_scope(isolate);
		console_hook_install_on_context(isolate, env->get_context());
	}
}
} //namespace
#endif // JSB_WITH_NODE

static int64_t bridge_add_console_output(void *p_userdata,
		void (*p_write)(void *p_userdata, int32_t p_severity, const char *p_text_utf8, int64_t p_length)) {
	if (!p_write) return -1;
#if JSB_WITH_NODE
	// first editor console sink in this process: arm the node console hook
	// and wrap the console of every live Environment
	console_hook_activate();
#endif
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
