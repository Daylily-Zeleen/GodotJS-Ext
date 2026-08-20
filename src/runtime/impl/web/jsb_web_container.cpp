/************************************************************************/
/*  jsb_web_container.cpp                                               */
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

#include "jsb_web_container.h"

#include "jsb_web_isolate.h"
#include "jsb_web_typedef.h"

namespace v8 {
Local<Array> Array::New(Isolate *isolate, int length) {
	return Local<Array>(Data(isolate, jsbi_NewArray(isolate->rt())));
}

uint32_t Array::Length() const {
	const int len = jsbi_GetArrayLength(isolate_->rt(), stack_pos_);
	return (uint32_t)len;
}

size_t Map::Size() const {
	return jsbi_MapSize(isolate_->rt(), stack_pos_);
}

Local<Array> Map::AsArray() const {
	return Local<Array>(Data(isolate_, jsbi_MapAsArray(isolate_->rt(), stack_pos_)));
}

MaybeLocal<Value> Map::Get(Local<Context> context, Local<Value> key) {
	const jsb::impl::StackPosition result_sp = jsbi_MapGetEntry(isolate_->rt(), stack_pos_, key->stack_pos_);
	if (result_sp < 0) {
		return MaybeLocal<Value>();
	}
	return MaybeLocal<Value>(Data(isolate_, result_sp));
}

MaybeLocal<Map> Map::Set(Local<Context> context, Local<Value> key, Local<Value> value) {
	const jsb::impl::ResultValue res = jsbi_MapSetEntry(isolate_->rt(), stack_pos_, key->stack_pos_, value->stack_pos_);
	if (res == -1) {
		return MaybeLocal<Map>();
	}
	return MaybeLocal<Map>(Data(isolate_, stack_pos_));
}

Local<Map> Map::New(Isolate *isolate) {
	return Local<Map>(Data(isolate, jsbi_NewMap(isolate->rt())));
}

size_t Set::Size() const {
	// Reuse MapSize — it checks instanceof and returns .size for both Map and Set
	return jsbi_MapSize(isolate_->rt(), stack_pos_);
}

Local<Array> Set::AsArray() const {
	return Local<Array>(Data(isolate_, jsbi_SetAsArray(isolate_->rt(), stack_pos_)));
}

MaybeLocal<Set> Set::Add(Local<Context> context, Local<Value> key) {
	const jsb::impl::ResultValue res = jsbi_SetAdd(isolate_->rt(), stack_pos_, key->stack_pos_);
	if (res == -1) {
		return MaybeLocal<Set>();
	}
	return MaybeLocal<Set>(Data(isolate_, stack_pos_));
}

Local<Set> Set::New(Isolate *isolate) {
	return Local<Set>(Data(isolate, jsbi_NewSet(isolate->rt())));
}

} //namespace v8
