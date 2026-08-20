/************************************************************************/
/*  jsb_ref.h                                                           */
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

#include "jsb_bridge_pch.h"
namespace jsb {
/**
 * a javascript object reference (weak)
 * @note it can be used as key of `std:unordered_map`, but can not be used as key of `HashMap` since `move` is not supported by `HashMap`
 */
template <typename T = v8::Object>
struct TWeakRef {
	struct hasher {
		_FORCE_INLINE_ size_t operator()(const TWeakRef &obj) const noexcept { return obj.hash(); }
	};
	struct equaler {
		_FORCE_INLINE_ bool operator()(const TWeakRef &lhs, const TWeakRef &rhs) const { return lhs == rhs; }
	};

	int hash_;
	v8::Global<T> object_;

	TWeakRef(v8::Isolate *p_isolate, const v8::Global<T> &p_object) {
		hash_ = p_object.Get(p_isolate)->GetIdentityHash();
		object_.Reset(p_isolate, p_object);
		object_.SetWeak();
	}

	TWeakRef(v8::Isolate *p_isolate, const v8::Local<T> &p_object) {
		hash_ = p_object->GetIdentityHash();
		object_.Reset(p_isolate, p_object);
		object_.SetWeak();
	}

	TWeakRef(const TWeakRef &) = delete;
	TWeakRef &operator=(const TWeakRef &p_other) noexcept = delete;

	~TWeakRef() = default;
	TWeakRef(TWeakRef &&p_other) noexcept = default;
	TWeakRef &operator=(TWeakRef &&p_other) noexcept = default;

	_FORCE_INLINE_ explicit operator bool() const { return !object_.IsEmpty(); }

	_FORCE_INLINE_ friend bool operator==(const TWeakRef &lhs, const TWeakRef &rhs) {
		return // lhs.hash_ == rhs.hash_ &&
				lhs.object_ == rhs.object_;
	}

	_FORCE_INLINE_ friend bool operator!=(const TWeakRef &lhs, const TWeakRef &rhs) {
		return !(lhs == rhs);
	}

	_FORCE_INLINE_ uint32_t hash() const {
		return (uint32_t)hash_;
	}
};

template <typename T = v8::Object>
struct TStrongRef {
	struct hasher {
		_FORCE_INLINE_ size_t operator()(const TStrongRef &obj) const noexcept { return obj.hash(); }
	};
	struct equaler {
		_FORCE_INLINE_ bool operator()(const TStrongRef &lhs, const TStrongRef &rhs) const { return lhs == rhs; }
	};

	int hash_;
	v8::Global<T> object_;
	int ref_count_;

	TStrongRef() : hash_(0), object_(), ref_count_(1) {}
	TStrongRef(v8::Isolate *p_isolate, const v8::Local<T> &p_object) {
		hash_ = p_object->GetIdentityHash();
		object_.Reset(p_isolate, p_object);
		ref_count_ = 1;
	}

	TStrongRef(const TStrongRef &) = delete;
	TStrongRef &operator=(const TStrongRef &p_other) noexcept = delete;

	~TStrongRef() = default;
	TStrongRef(TStrongRef &&p_other) noexcept = default;
	TStrongRef &operator=(TStrongRef &&p_other) noexcept = default;

	_FORCE_INLINE_ explicit operator bool() const { return !object_.IsEmpty(); }

	void ref() {
		jsb_check(ref_count_ > 0);
		++ref_count_;
	}
	bool unref() {
		jsb_check(ref_count_ > 0);
		return --ref_count_ == 0;
	}

	_FORCE_INLINE_ friend bool operator==(const TStrongRef &lhs, const TStrongRef &rhs) {
		return // lhs.hash_ == rhs.hash_ &&
				lhs.object_ == rhs.object_;
	}

	_FORCE_INLINE_ friend bool operator!=(const TStrongRef &lhs, const TStrongRef &rhs) {
		return !(lhs == rhs);
	}

	_FORCE_INLINE_ uint32_t hash() const {
		return (uint32_t)hash_;
	}
};
} //namespace jsb

