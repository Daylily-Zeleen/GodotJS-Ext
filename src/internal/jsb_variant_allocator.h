/************************************************************************/
/*  jsb_variant_allocator.h                                             */
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

#include "jsb_internal_pch.h"
#include "jsb_macros.h"

#include <compat/paged_allocator.h>
#include <godot_cpp/templates/spin_lock.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace jsb::internal {
class VariantAllocator {
	bool use_front_ = true;
	Vector<Variant *> front_;
	Vector<Variant *> back_;
	SpinLock spin_lock_;

#if JSB_DEBUG
	SafeNumeric<uint32_t> alive_variants_num_;
#endif

#if JSB_WITH_V8 || JSB_WITH_JAVASCRIPTCORE
	// v8 and jsc implementation of deleter are probably called from JS gc threads
	// so we need thread-safe version
	PagedAllocator<Variant, true> paged_allocator_;
#else
	// web and quickjs implementation of deleter are called from owner thread only
	PagedAllocator<Variant, false> paged_allocator_;
#endif

public:
#if JSB_DEBUG
	_FORCE_INLINE_ uint32_t get_allocated_num() const { return alive_variants_num_.get(); }
#else
	// intentionally ignored in release mode
	_FORCE_INLINE_ uint32_t get_allocated_num() const { return 0; }
#endif

#if JSB_DEBUG
	~VariantAllocator() {
		spin_lock_.lock();
		jsb_notice(front_.is_empty() && back_.is_empty(), "the pending queue is not empty");
		spin_lock_.unlock();
		jsb_notice(get_allocated_num() == 0, "variant pool leaked");
	}
#endif

	_FORCE_INLINE_ Variant *alloc(const Variant &p_templet) {
		Variant *rval = alloc();
		*rval = p_templet;
		return rval;
	}

	_FORCE_INLINE_ Variant *alloc() {
		increment();
		return paged_allocator_.alloc();
	}

	//NOTE safe to call from other threads only if p_var is not reference-based type
	_FORCE_INLINE_ void free(Variant *p_var) {
		decrement();
		paged_allocator_.free(p_var);
	}

	// gc thread
	void free_safe(Variant *p_var) {
		spin_lock_.lock();
		if (use_front_) front_.push_back(p_var);
		else back_.push_back(p_var);
		spin_lock_.unlock();
	}

	// should only be called on owner thread
	void drain() {
		spin_lock_.lock();
		Vector<Variant *> &queue = use_front_ ? front_ : back_;
		use_front_ = !use_front_;
		spin_lock_.unlock();

		if (!queue.is_empty()) {
			for (Variant *variant : queue) {
				free(variant);
			}
			queue.clear();
		}
	}

private:
#if JSB_DEBUG
	_FORCE_INLINE_ void increment() { alive_variants_num_.increment(); }
	_FORCE_INLINE_ void decrement() { alive_variants_num_.decrement(); }
#else
	_FORCE_INLINE_ void increment() {}
	_FORCE_INLINE_ void decrement() {}
#endif
};
} //namespace jsb::internal

