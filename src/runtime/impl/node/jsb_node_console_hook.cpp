/************************************************************************/
/*  jsb_node_console_hook.cpp                                           */
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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU    */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#include "jsb_node_console_hook.h"

#include "jsb_node.h"

#include "../../bridge/jsb_bridge_helper.h"
#include "../../bridge/jsb_environment.h"
#include <internal/jsb_console_output.h>
#include <string_builder.h>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/templates/hash_map.hpp>

namespace jsb::impl {
namespace {

bool s_console_hook_active = false;

// mirror one line into the console sinks (IConsoleOutput list)
void console_hook_write(internal::ELogSeverity::Type p_severity, const String &p_text) {
	internal::IConsoleOutput::internal_write(p_severity, p_text);
}

// join "[JS] arg1 arg2 ..." with the same BridgeHelper::stringify formatting
// as the Essentials console implementation
template <internal::ELogSeverity::Type Severity>
void console_log_wrap(const v8::FunctionCallbackInfo<v8::Value> &info) {
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
void console_assert_wrap(const v8::FunctionCallbackInfo<v8::Value> &info) {
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

// console.time/timeEnd: fully taken over. The elapsed value only exists on the
// C++ side; the node-native timer writes to stdout only and cannot reach the
// sinks. Not forwarded.
//
// The tag table lives here rather than on Environment, which must stay
// engine-only in the node build. It keeps the Essentials semantics of one tag
// table per Environment: in node mode Environment and Isolate are 1:1 (each
// Environment owns a NodeRuntime that creates its own isolate), so keying by
// isolate is exactly per-environment. Labels are compared as plain utf8
// strings -- the same semantics as the native console.time label matching,
// without the isolate-bound TStrongRef<v8::String> bookkeeping.
//
// Lifetime: NodeRuntime's destructor calls console_hook_drop_isolate(), so
// entries never outlive their isolate.
HashMap<v8::Isolate *, HashMap<String, uint64_t>> s_timer_tags;

// resolve the timer label from the first argument ('default' when undefined)
String console_time_label(const v8::FunctionCallbackInfo<v8::Value> &info) {
	v8::Isolate *isolate = info.GetIsolate();
	return info[0]->IsUndefined() ? String("default") : impl::Helper::to_string(isolate, info[0]);
}

void console_time_wrap(const v8::FunctionCallbackInfo<v8::Value> &info) {
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

void console_time_end_wrap(const v8::FunctionCallbackInfo<v8::Value> &info) {
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
void console_hook_install_on_context(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context) {
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

void console_hook_ensure(v8::Isolate *p_isolate, const v8::Local<v8::Context> &p_context) {
	if (!s_console_hook_active) {
		return;
	}
	// called by NodeRuntime's constructor right after the bootstrap made the
	// console available on the given context; the caller holds the scope
	console_hook_install_on_context(p_isolate, p_context);
}

void console_hook_drop_isolate(v8::Isolate *p_isolate) {
	// called by NodeRuntime's destructor: drop the console hook state owned by
	// the releasing isolate (the timer tag table), mirroring how the Essentials
	// tag table dies with its Environment
	s_timer_tags.erase(p_isolate);
}

void console_hook_arm() {
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

} //namespace jsb::impl
