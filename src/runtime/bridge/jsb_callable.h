/************************************************************************/
/*  jsb_callable.h                                                      */
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

#pragma once

#include "jsb_bridge.h"
#include <common/compat/misc.h>
namespace jsb {
class JSCallable : public CallableCustom {
private:
	godot::ObjectID object_id_;
	jsb::ObjectCacheID callback_id_;
	jsb::EnvironmentID env_id_;

public:
	static bool _compare_equal(const CallableCustom *p_a, const CallableCustom *p_b) {
		// types are already ensured by `Callable::operator==` with the comparator function pointers before calling
		const JSCallable *js_cc_a = (const JSCallable *)p_a;
		const JSCallable *js_cc_b = (const JSCallable *)p_b;
		return js_cc_a->callback_id_ == js_cc_b->callback_id_;
	}

	static bool _compare_less(const CallableCustom *p_a, const CallableCustom *p_b) {
		const JSCallable *js_cc_a = (const JSCallable *)p_a;
		const JSCallable *js_cc_b = (const JSCallable *)p_b;
		return js_cc_a->callback_id_ < js_cc_b->callback_id_;
		// return !_compare_equal(p_a, p_b) && p_a < p_b;
	}

	JSCallable(godot::ObjectID p_object_id, jsb::EnvironmentID p_env_id, jsb::ObjectCacheID p_callback_id)
			: object_id_(p_object_id), callback_id_(p_callback_id), env_id_(p_env_id) {
	}

	virtual ~JSCallable() override;

	/**
	 * it's a free callable object if object_id_ is explicitly assigned as zero.
	 * otherwise, do the same thing in CallableCustom::is_valid().
	 */
	virtual bool is_valid() const override { return object_id_.is_null() || godot::ObjectDB::get_instance(object_id_); }

	virtual String get_as_text() const override;
	virtual godot::ObjectID get_object() const override { return object_id_; }
	virtual void call(const Variant **p_arguments, int p_argcount, Variant &r_return_value, GDExtensionCallError &r_call_error) const override;

	virtual CompareEqualFunc get_compare_equal_func() const override { return _compare_equal; }
	virtual CompareLessFunc get_compare_less_func() const override { return _compare_less; }
	virtual uint32_t hash() const override { return callback_id_.hash(); }
};
} //namespace jsb

