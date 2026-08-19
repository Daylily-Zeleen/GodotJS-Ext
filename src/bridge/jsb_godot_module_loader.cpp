/************************************************************************/
/*  jsb_godot_module_loader.cpp                                         */
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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU   */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#include "jsb_godot_module_loader.h"
#include "jsb_environment.h"
#include "jsb_object_bindings.h"
#include "jsb_type_convert.h"

#include <api_tool/api_tool.h>
#include <api_tool/api_tool_types.h>

#include <godot_cpp/classes/engine.hpp>

namespace jsb {
// JS function (type_name: string): type
// it's called from JS, load godot type with the `type_name` in the `godot` module (it can be type/singleton/constant/etc.)
// [JS] function load_type(type_name: string): Class;
static void _load_godot_object_class(const v8::FunctionCallbackInfo<v8::Value> &info) {
	JSB_BENCHMARK_SCOPE(JSRealm, _load_godot_mod);

	v8::Isolate *isolate = info.GetIsolate();
	const v8::Local<v8::Value> arg0 = info[0];
	if (!arg0->IsString()) {
		jsb_throw(isolate, "bad parameter");
		return;
	}

	Environment *env = Environment::wrap(isolate);
	jsb_check(env);
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();
	const StringName p_type_name = impl::Helper::to_string(isolate, arg0);
	if (internal::StringNames::get_singleton().is_replaced_name(p_type_name)) {
		JSB_LOG(Warning,
				"please use replaced name '%s' for '%s' in scripts (regenerate d.ts files)",
				p_type_name,
				internal::StringNames::get_singleton().get_replaced_name(p_type_name));
	}
	const StringName original_name = internal::StringNames::get_singleton().get_original_name(p_type_name);

	//NOTE do not break the order in `GDScriptLanguage::init()`

	// (1) singletons have the top priority (in GDScriptLanguage::init, singletons will overwrite the globals slot even if a type/const has the same name)
	//     check before getting to avoid error prints in `get_singleton_object`
	if (Engine::get_singleton()->has_singleton(original_name)) {
		if (Object *gd_singleton = Engine::get_singleton()->get_singleton(original_name)) {
			JSB_LOG(VeryVerbose, "exposing singleton object %s", (String)original_name);
			if (v8::Local<v8::Object> rval;
					TypeConvert::gd_obj_to_js(isolate, context, gd_singleton, rval) && !rval.IsEmpty()) {
				env->mark_as_persistent_object(gd_singleton);
				info.GetReturnValue().Set(rval);
				return;
			}
			jsb_throw(isolate, "failed to bind a singleton object");
			return;
		}
	}

	// (2) (global) utility functions.
	if (api_tool::has_utility_function(original_name)) {
		//TODO check static bindings at first, and dynamic bindings as a fallback

		const auto *api_utility_function = api_tool::find_utility_function(original_name);
		jsb_check(api_utility_function != nullptr);

		// dynamic binding:
		const int32_t utility_func_index = (int32_t)env->get_variant_info_collection().utility_funcs.size();
		env->get_variant_info_collection().utility_funcs.append({});
		internal::FUtilityMethodInfo &method_info = env->get_variant_info_collection().utility_funcs.write[utility_func_index];

		const godot::MethodInfo &method = api_utility_function->method;

		const int argument_count = method.arguments.size();
		method_info.argument_types.resize(argument_count);
		for (int index = 0, num = argument_count; index < num; ++index) {
			method_info.argument_types.write[index] = method.arguments[index].type;
		}
		//NOTE currently, utility functions have no default argument.
		// method_info.default_arguments = ...
		method_info.return_type = method.return_val.type;
		method_info.is_vararg = api_utility_function->is_vararg();
		method_info.set_debug_name(internal::NamingUtil::get_member_name(original_name));
		method_info.utility_func = api_utility_function;
		JSB_LOG(VeryVerbose, "expose godot utility function %s (%d)", original_name, utility_func_index);
		jsb_check(method_info.utility_func);

		info.GetReturnValue().Set(JSB_NEW_FUNCTION(context, ObjectReflectBindingUtil::_godot_utility_func, v8::Int32::New(isolate, utility_func_index)));
		return;
	}

	// (3) global_constants
	if (api_tool::has_global_constant(original_name)) {
		const auto *api_constant = api_tool::find_global_constant(original_name);
		jsb_check(api_constant != nullptr);
		JSB_LOG(VeryVerbose, "expose godot global constant %s", original_name);
		info.GetReturnValue().Set(impl::Helper::new_integer(isolate, api_constant->value));
		return;
	}

	// (4) classes in ClassDB/PrimitiveTypes
	{
		if (const NativeClassInfoPtr class_info = env->expose_class(p_type_name)) {
			jsb_check(class_info->name == p_type_name);
			jsb_check(!class_info->clazz.IsEmpty());
			info.GetReturnValue().Set(class_info->clazz.Get(isolate));
			return;
		}

		// dynamic binding: godot class types
		if (api_tool::has_class(original_name)) {
			const api_tool::ApiClass *api_class = api_tool::find_class(original_name);
			jsb_check(api_class != nullptr);
			if (const NativeClassInfoPtr class_info = env->expose_godot_object_class(original_name)) {
				jsb_check(class_info->name == p_type_name);
				jsb_check(!class_info->clazz.IsEmpty());
				info.GetReturnValue().Set(class_info->clazz.Get(isolate));
				return;
			}
		}
	}

	// (5) global_enums
	if (api_tool::has_global_enum(original_name)) {
		const auto &api_enum_info = api_tool::find_global_enum(original_name);
		jsb_check(api_enum_info != nullptr);
		info.GetReturnValue().Set(BridgeHelper::to_global_enum(isolate, context, api_enum_info));
		return;
	}

	// (6) special case: `Variant` (`Variant` is not exposed as it-self in js, but we still need to access the nested enums in it)
	// seealso: core/variant/binder_common.h
	//          VARIANT_ENUM_CAST(Variant::Type);
	//          VARIANT_ENUM_CAST(Variant::Operator);
	// they are exposed as `Variant.Type` in global constants in godot
	if (original_name == jsb_string_name(Variant)) {
		const v8::Local<v8::Object> obj = v8::Object::New(isolate);
		obj->Set(context, impl::Helper::new_string(isolate, "Type"), BridgeHelper::to_global_enum(isolate, context, "Variant.Type")).Check();
		obj->Set(context, impl::Helper::new_string(isolate, "Operator"), BridgeHelper::to_global_enum(isolate, context, "Variant.Operator")).Check();
		info.GetReturnValue().Set(obj);
		return;
	}

	jsb_throw(isolate, jsb_format("godot class not found '%s'", original_name));
}

v8::Local<v8::Object> GodotModuleLoader::_get_loader_proxy(Environment *p_env) {
	if (!loader_.IsEmpty()) return loader_.Get(p_env->get_isolate());
	const v8::Local<v8::Context> context = p_env->get_context();
	const JavaScriptModule *typeloader = p_env->get_module_cache().find(jsb_string_name(godot_typeloader));
	jsb_checkf(typeloader != nullptr, "Can't find \"%s\" module in the module cache!", jsb_string_name(godot_typeloader));
	const v8::Local<v8::Value> typeloader_exports = typeloader->exports.Get(p_env->get_isolate());
	jsb_check(!typeloader_exports.IsEmpty() && typeloader_exports->IsObject());
	// not using string cache, as it's run only once
	const v8::Local<v8::Value> proxy_func_val = typeloader_exports.As<v8::Object>()->Get(context, impl::Helper::new_string_ascii(p_env->get_isolate(), "_mod_proxy_")).ToLocalChecked();
	jsb_check(!proxy_func_val.IsEmpty() && proxy_func_val->IsFunction());
	const v8::Local<v8::Function> proxy_func = proxy_func_val.As<v8::Function>();
	{
		v8::Isolate *isolate = p_env->get_isolate();
		v8::Local<v8::Object> builtin_symbols = v8::Object::New(isolate);
		builtin_symbols->Set(context, impl::Helper::new_string_ascii(isolate, "FloatType"), p_env->get_symbol(Symbols::FloatType));
		builtin_symbols->Set(context, impl::Helper::new_string_ascii(isolate, "IntegerType"), p_env->get_symbol(Symbols::IntegerType));
		builtin_symbols->Set(context, impl::Helper::new_string_ascii(isolate, "ProxyTarget"), p_env->get_symbol(Symbols::ProxyTarget));
		v8::Local<v8::Value> argv[] = { builtin_symbols, JSB_NEW_FUNCTION(context, _load_godot_object_class, {}) };
		const v8::MaybeLocal<v8::Value> result = proxy_func->Call(context, v8::Undefined(isolate), std::size(argv), argv);

		if (v8::Local<v8::Value> proxy; result.ToLocal(&proxy)) {
#if JSB_WITH_V8
			jsb_check(proxy->IsProxy());
#endif
			loader_.Reset(p_env->get_isolate(), proxy.As<v8::Object>());
			return proxy.As<v8::Object>();
		}
		// empty means error thrown in Call()
	}

	// empty means error thrown in _compile_run()
	return {};
}

bool GodotModuleLoader::load(Environment *p_env, JavaScriptModule &p_module) {
	v8::Isolate *isolate = p_env->get_isolate();
	JSB_ISOLATE_SCOPE(isolate);
	v8::HandleScope handle_scope(isolate);
	v8::Local<v8::Context> context = p_env->get_context();
	v8::Context::Scope context_scope(context);

	const v8::Local<v8::Object> loader = _get_loader_proxy(p_env);
	if (loader.IsEmpty()) {
		return false;
	}

	// it's a proxy object which will load godot type on-demand until it's actually accessed in a script
	p_module.exports.Reset(isolate, loader);
	return true;
}

} //namespace jsb
