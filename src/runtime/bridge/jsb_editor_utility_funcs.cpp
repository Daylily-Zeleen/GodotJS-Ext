/************************************************************************/
/*  jsb_editor_utility_funcs.cpp                                        */
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

#include "jsb_editor_utility_funcs.h"

namespace jsb {
namespace {
EditorUtilityFuncs::ExposeFunc g_expose_impl = nullptr;

void _editor_only(const v8::FunctionCallbackInfo<v8::Value> &info) {
	jsb_throw(info.GetIsolate(), "jsb.editor methods are only available in editor builds");
}

void _expose_stub(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> jsb_obj) {
	// No editor library loaded (game runtime, or editor library not yet initialized):
	// install an `editor` object whose members all throw when called.
	v8::Local<v8::Object> editor_obj = v8::Object::New(isolate);
	v8::Local<v8::Function> editor_only = JSB_NEW_FUNCTION(context, _editor_only, {});

	jsb_obj->Set(context, impl::Helper::new_string_ascii(isolate, "editor"), editor_obj).Check();
	editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "get_class_doc"), editor_only).Check();
	editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "get_classes"), editor_only).Check();
	editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "get_global_constants"), editor_only).Check();
	editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "get_singletons"), editor_only).Check();
	editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "get_utility_functions"), editor_only).Check();
	editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "get_primitive_types"), editor_only).Check();
	editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "get_input_actions"), editor_only).Check();
	editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "delete_file"), editor_only).Check();
	editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "install_project_files"), editor_only).Check();
	editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "install_static_types"), editor_only).Check();
	editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "generate_types"), editor_only).Check();
	editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "cleanup_invalid_files"), editor_only).Check();
	editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "VERSION_DOCS_URL"), impl::Helper::new_string_ascii(isolate, "")).Check();
}
} //namespace

void EditorUtilityFuncs::set_expose_impl(ExposeFunc p_func) {
	g_expose_impl = p_func;
}

void EditorUtilityFuncs::expose(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> jsb_obj) {
	if (g_expose_impl) {
		g_expose_impl(isolate, context, jsb_obj);
	} else {
		_expose_stub(isolate, context, jsb_obj);
	}
}
} //namespace jsb
