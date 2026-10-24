/************************************************************************/
/*  jsb_engine_singleton_binding.cpp                                    */
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

#include "jsb_engine_singleton_binding.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/gdextension_interface_loader.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/templates/mutex.hpp>

namespace jsb {

// require 到引擎单例时的 binding 登记表：name -> instance_id。
// 只存 id 不存指针：部分单例在 CORE 反初始化之前就被引擎销毁（binding 自然触发、
// wrapper 已释放），届时持指针的一方（godot::ClassDB::engine_singletons）会变成悬空表。
namespace {
Mutex registered_singletons_mutex;
HashMap<StringName, uint64_t> registered_engine_singletons;
} // namespace

void register_engine_singleton_binding(const StringName &p_name, Object *p_singleton) {
	MutexLock lock(registered_singletons_mutex);
	registered_engine_singletons.insert(p_name, p_singleton->get_instance_id());
	// 幂等（godot-cpp 按名去重，内部有锁），重复注册无副作用。
	ClassDB::_register_engine_singleton(p_name, p_singleton);
}

void release_engine_singleton_bindings() {
	LocalVector<StringName> names;
	{
		MutexLock lock(registered_singletons_mutex);
		for (const KeyValue<StringName, uint64_t> &kv : registered_engine_singletons) {
			names.push_back(kv.key);
		}
	}
	for (const StringName &name : names) {
		uint64_t id = 0;
		{
			MutexLock lock(registered_singletons_mutex);
			id = registered_engine_singletons[name];
			registered_engine_singletons.erase(name);
		}
		// 只信 instance_id：对象若已销毁（binding 已自然归还、wrapper 已释放），
		// 按存活校验直接跳过——绝不再经旧指针读任何成员。
		GDExtensionObjectPtr raw = godot::gdextension_interface::object_get_instance_from_id(id);
		if (raw != nullptr) {
			godot::gdextension_interface::object_free_instance_binding(raw, godot::gdextension_interface::token);
		}
		// 从 godot-cpp 的 CORE 盲释放表摘除：其循环不解引用存活校验，会对表内
		// 悬空指针调 object_free_instance_binding（即曾实测的堆损坏）。
		ClassDB::_unregister_engine_singleton(name);
	}
}

} // namespace jsb
