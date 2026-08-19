/************************************************************************/
/*  jsb_value_move.cpp                                                  */
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

#include "jsb_value_move.h"
#include "jsb_bridge_helper.h"
#include "jsb_environment.h"
#include "jsb_type_convert.h"

namespace jsb {
JSValueMove::JSValueMove(const std::shared_ptr<Environment> &p_env, const v8::Local<v8::Value> &p_value) {
	jsb_check(p_env);
	env_ = p_env;
	value_.Reset(env_->get_isolate(), p_value);
}

bool JSValueMove::is_valid() const {
	return env_ && !value_.IsEmpty();
}

Variant JSValueMove::to_variant() const {
	if (!is_valid()) return {};
	v8::Isolate *isolate = env_->get_isolate();
	JSB_ISOLATE_SCOPE(isolate);
	v8::HandleScope handle_scope(isolate);
	v8::Local<v8::Context> context = env_->get_context();
	v8::Context::Scope context_scope(context);

	Variant val;
	TypeConvert::js_to_gd_var(isolate, context, value_.Get(isolate), val);
	return val;
}

String JSValueMove::to_string() const {
	if (!is_valid()) return {};
	v8::Isolate *isolate = env_->get_isolate();
	JSB_ISOLATE_SCOPE(isolate);
	v8::HandleScope handle_scope(isolate);
	v8::Local<v8::Context> context = env_->get_context();
	v8::Context::Scope context_scope(context);

	return BridgeHelper::stringify(isolate, value_.Get(isolate));
}
} //namespace jsb
