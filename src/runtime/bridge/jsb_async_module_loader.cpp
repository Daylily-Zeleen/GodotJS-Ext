/************************************************************************/
/*  jsb_async_module_loader.cpp                                         */
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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU    */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#include "jsb_async_module_loader.h"

#include "jsb_environment.h"

namespace jsb {
namespace {
template <bool is_fulfilled>
void js_on_finish(const v8::FunctionCallbackInfo<v8::Value> &info) {
	v8::Isolate *isolate = info.GetIsolate();
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();
	Environment *env = Environment::wrap(isolate);

	jsb_check(!info.Data().IsEmpty() && info.Data()->IsUint32());
	const AsyncModuleToken token(info.Data().As<v8::Uint32>()->Value());
	env->get_async_module_manager()._mark_as_handled(context, token, is_fulfilled, info[0]);
}
} //namespace

bool AsyncModuleHandle::is_valid() const {
	if (env_ && !!token_) {
		if (const auto env = Environment::_access(env_)) {
			return env->get_async_module_manager().is_valid(token_);
		}
	}
	return false;
}

bool AsyncModuleHandle::resolve(const String &p_source) {
	if (const auto env = Environment::_access(env_)) {
		//TODO env -> enqueue async call
		jsb_not_implemented(true, "resolve from handle is not implemented yet");
	}
	return false;
}

bool AsyncModuleHandle::reject(const String &p_error) {
	if (const auto env = Environment::_access(env_)) {
		//TODO env -> enqueue async call
		jsb_not_implemented(true, "reject from handle is not implemented yet");
	}
	return false;
}

void ScriptableAsyncModuleLoader::on_detached() {
	func_.Reset();
}

void ScriptableAsyncModuleLoader::import(Environment &p_env, const StringName &p_module_id, AsyncModuleHandle p_handle) {
	v8::Isolate *isolate = p_env.get_isolate();
	const v8::Local<v8::Function> func = func_.Get(isolate);
	const v8::Local<v8::Context> context = p_env.get_context();
	const v8::Local<v8::String> module_id = p_env.get_string_value(p_module_id);
	const v8::Local<v8::Value> data = v8::Uint32::NewFromUnsigned(isolate, *p_handle.token());
	v8::Local<v8::Value> args[] = {
		module_id,
		/* resolve */ v8::Function::New(context, js_on_finish<true>, data, 1).ToLocalChecked(),
		/* reject  */ v8::Function::New(context, js_on_finish<false>, data, 1).ToLocalChecked(),
	};
	// 用 TryCatch 包住调用：回调是用户 JS，抛错时 `ToLocalChecked()` 会以 CHECK 失败而不是把异常
	// 交给宿主（同 `AMDModuleLoader::load_source` 的处理，`jsb_amd_module_loader.cpp:80-88`）。
	impl::TryCatch try_catch(isolate);
	const v8::MaybeLocal<v8::Value> ret = func->Call(context, v8::Undefined(isolate), std::size(args), args);
	if (try_catch.has_caught()) {
		JSB_LOG(Error, "failed to invoke the async module loader: %s", BridgeHelper::get_exception(try_catch));
		return;
	}
	// 约定：loader 返回 undefined（它通过 resolve/reject 句柄回传结果）。
	if (v8::Local<v8::Value> result; ret.ToLocal(&result)) {
		jsb_check(result->IsUndefined());
	} else {
		// 异常已被上面的 TryCatch 处理；`ToLocal` 失败只可能来自该异常。
		jsb_check(try_catch.has_caught());
	}
}

} //namespace jsb
