/************************************************************************/
/*  jsb_object_db.h                                                     */
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

#include "../../common/compat/jsb_compat.h"
#include "jsb_bridge_pch.h"
#include "jsb_object_handle.h"

#include "compat/rw_lock.h"
#include <godot_cpp/templates/hash_map.hpp>

namespace jsb {
class BindingObjectDB;

#if JSB_THREADING
#	define JSB_OBJECT_DB_HANDLE(Type, Ptr) Type(&lock_, Ptr)
#	define JSB_OBJECT_DB_STATEMENT(Statement) Statement
#	define JSB_OBJECT_DB_PREPARE_FOR_REMOVAL(Handle) Handle.prepare_for_removal()
#else
#	define JSB_OBJECT_DB_HANDLE(Type, Ptr) (sizeof(Type), Ptr)
#	define JSB_OBJECT_DB_STATEMENT(Statement) (void)0
#	define JSB_OBJECT_DB_PREPARE_FOR_REMOVAL(Handle) Handle = nullptr
#endif

#if JSB_THREADING
struct ObjectHandlePtr {
private:
	friend class BindingObjectDB;

	RWLock *lock_;
	internal::SArray<ObjectHandle, NativeObjectID>::Pointer ptr_;

	// Release slot address scope while preserving BindingObjectDB write lock.
	_FORCE_INLINE_ void prepare_for_removal() { ptr_ = nullptr; }

public:
	ObjectHandlePtr(const ObjectHandlePtr &) = delete;

	ObjectHandlePtr() : lock_(nullptr) {}
	ObjectHandlePtr(RWLock *p_lock, internal::SArray<ObjectHandle, NativeObjectID>::Pointer &&p_ptr)
			: lock_(p_lock), ptr_(std::move(p_ptr)) {
	}

	~ObjectHandlePtr() {
		if (lock_) lock_->write_unlock();
	}

	ObjectHandle *operator->() const { return ptr_.operator->(); }
	explicit operator bool() const { return (bool)ptr_; }

	ObjectHandlePtr &operator=(std::nullptr_t) {
		if (lock_) lock_->write_unlock();
		lock_ = nullptr;
		ptr_ = nullptr;
		return *this;
	}

	ObjectHandlePtr(ObjectHandlePtr &&p_other) noexcept
			: lock_(p_other.lock_), ptr_(std::move(p_other.ptr_)) {
		p_other.lock_ = nullptr;
	}

	ObjectHandlePtr &operator=(ObjectHandlePtr &&p_other) noexcept {
		if (this != &p_other) {
			if (lock_) lock_->write_unlock();
			lock_ = p_other.lock_;
			ptr_ = std::move(p_other.ptr_);
			p_other.lock_ = nullptr;
		}
		return *this;
	}
};

struct ObjectHandleConstPtr {
private:
	const RWLock *lock_;
	internal::SArray<ObjectHandle, NativeObjectID>::ConstPointer ptr_;

public:
	ObjectHandleConstPtr(const ObjectHandleConstPtr &) = delete;

	ObjectHandleConstPtr() : lock_(nullptr) {}
	ObjectHandleConstPtr(const RWLock *p_lock, internal::SArray<ObjectHandle, NativeObjectID>::ConstPointer &&p_ptr)
			: lock_(p_lock), ptr_(std::move(p_ptr)) {
	}

	~ObjectHandleConstPtr() {
		if (lock_) lock_->read_unlock();
	}

	const ObjectHandle *operator->() const { return ptr_.operator->(); }
	explicit operator bool() const { return (bool)ptr_; }
	ObjectHandleConstPtr &operator=(std::nullptr_t) {
		if (lock_) lock_->read_unlock();
		lock_ = nullptr;
		ptr_ = nullptr;
		return *this;
	}

	ObjectHandleConstPtr(ObjectHandleConstPtr &&p_other) noexcept
			: lock_(p_other.lock_), ptr_(std::move(p_other.ptr_)) {
		p_other.lock_ = nullptr;
	}

	ObjectHandleConstPtr &operator=(ObjectHandleConstPtr &&p_other) noexcept {
		if (this != &p_other) {
			if (lock_) lock_->read_unlock();
			lock_ = p_other.lock_;
			ptr_ = std::move(p_other.ptr_);
			p_other.lock_ = nullptr;
		}
		return *this;
	}
};
#else
typedef internal::SArray<ObjectHandle, NativeObjectID>::Pointer ObjectHandlePtr;
typedef internal::SArray<ObjectHandle, NativeObjectID>::ConstPointer ObjectHandleConstPtr;
#endif

class BindingObjectDB {
private:
	// cpp objects should be added here since the gc callback is not guaranteed by v8
	// we need to delete them on finally releasing Environment
	internal::SArray<ObjectHandle, NativeObjectID> objects_;

	// (unsafe) mapping object pointer to object_id
	HashMap<void *, NativeObjectID> objects_index_;

#if JSB_THREADING
	RWLock lock_;
#endif

	// Remove object entry while caller already holds BindingObjectDB write lock.
	_FORCE_INLINE_ void remove_object_internal(void *p_pointer) {
		const NativeObjectID *entry = objects_index_.getptr(p_pointer);
		jsb_check(entry);
		objects_.remove_at_checked(*entry);
		objects_index_.erase(p_pointer);
	}

public:
	BindingObjectDB(int p_capacity) {
		objects_.reserve(p_capacity);
	}

	~BindingObjectDB() {
		jsb_check(objects_.size() == 0);
		jsb_check(objects_index_.size() == 0);
	}

	_FORCE_INLINE_ int size() const { return objects_.size(); }

	_FORCE_INLINE_ bool has_object(void *p_pointer) const {
		JSB_OBJECT_DB_STATEMENT(RWLockRead lock(lock_));
		return objects_index_.has(p_pointer);
	}

	_FORCE_INLINE_ bool has_object(const NativeObjectID &p_object_id) const {
		JSB_OBJECT_DB_STATEMENT(RWLockRead lock(lock_));
		return objects_.is_valid_index(p_object_id);
	}

	_FORCE_INLINE_ void *try_get_first_pointer() const {
		JSB_OBJECT_DB_STATEMENT(RWLockRead lock(lock_));
		return objects_index_.size() ? objects_index_.begin()->key : nullptr;
	}

	_FORCE_INLINE_ NativeObjectID try_get_object_id(void *p_pointer) const {
		JSB_OBJECT_DB_STATEMENT(RWLockRead lock(lock_));
		const NativeObjectID *it = objects_index_.getptr(p_pointer);
		return it ? *it : NativeObjectID();
	}

	// whether the `p_pointer` registered in the object binding map
	// return true, and the corresponding JS value if `p_pointer` is valid
	_FORCE_INLINE_ ObjectHandleConstPtr try_get_object(void *p_pointer) const {
		JSB_OBJECT_DB_STATEMENT(lock_.read_lock());

		const NativeObjectID *entry = objects_index_.getptr(p_pointer);
		if (entry) return JSB_OBJECT_DB_HANDLE(ObjectHandleConstPtr, objects_.get_value_scoped(*entry));

		JSB_OBJECT_DB_STATEMENT(lock_.read_unlock());
		return ObjectHandleConstPtr();
	}

	// [MUTABLE]
	_FORCE_INLINE_ ObjectHandlePtr try_get_object(void *p_pointer) {
		JSB_OBJECT_DB_STATEMENT(lock_.write_lock());

		const NativeObjectID *entry = objects_index_.getptr(p_pointer);
		if (entry) return JSB_OBJECT_DB_HANDLE(ObjectHandlePtr, objects_.get_value_scoped(*entry));

		JSB_OBJECT_DB_STATEMENT(lock_.write_unlock());
		return ObjectHandlePtr();
	}

	_FORCE_INLINE_ ObjectHandleConstPtr try_get_object(const NativeObjectID &p_object_id) const {
		JSB_OBJECT_DB_STATEMENT(lock_.read_lock());
		return JSB_OBJECT_DB_HANDLE(ObjectHandleConstPtr, objects_.try_get_value_scoped(p_object_id));
	}

	// will crash if the object is not registered in the object binding map
	_FORCE_INLINE_ ObjectHandleConstPtr get_object(const NativeObjectID &p_object_id) const {
		JSB_OBJECT_DB_STATEMENT(lock_.read_lock());
		return JSB_OBJECT_DB_HANDLE(ObjectHandleConstPtr, objects_.get_value_scoped(p_object_id));
	}

	// [MUTABLE]
	NativeObjectID add_object(void *p_pointer, ObjectHandlePtr *o_handle) {
		JSB_OBJECT_DB_STATEMENT(lock_.write_lock());
		if (const NativeObjectID *existing_object_id_ptr = objects_index_.getptr(p_pointer)) {
			const NativeObjectID existing_object_id = *existing_object_id_ptr;
			ObjectHandle &existing_handle = objects_.get_value(existing_object_id);

			if (existing_handle.ref_.IsEmpty()) {
				objects_.remove_at_checked(existing_object_id);
				objects_index_.erase(p_pointer);
			} else {
				if (o_handle) *o_handle = JSB_OBJECT_DB_HANDLE(ObjectHandlePtr, objects_.get_value_scoped(existing_object_id));
				else JSB_OBJECT_DB_STATEMENT(lock_.write_unlock());
				return existing_object_id;
			}
		}

		const NativeObjectID object_id = objects_.add({});
		objects_index_.insert(p_pointer, object_id);

		if (o_handle) *o_handle = JSB_OBJECT_DB_HANDLE(ObjectHandlePtr, objects_.get_value_scoped(object_id));
		else JSB_OBJECT_DB_STATEMENT(lock_.write_unlock());
		return object_id;
	}

	// [MUTABLE]
	_FORCE_INLINE_ void remove_object(ObjectHandlePtr &p_handle, void *p_pointer) {
#if JSB_DEBUG
		jsb_check(p_handle->pointer == p_pointer);
#endif
		JSB_OBJECT_DB_PREPARE_FOR_REMOVAL(p_handle);
		remove_object_internal(p_pointer);
		p_handle = nullptr;
	}
};
} //namespace jsb
