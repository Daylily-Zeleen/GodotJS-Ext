/************************************************************************/
/*  test_jsb_sarray.h                                                   */
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

#include "jsb_test_helpers.h"

// all internal::SArray test cases
namespace jsb::tests {
inline String get_test_name() { return "xxx.yyy.zzz.000"; }

struct Movable {
	int anything = 0;
	void *ptr = nullptr;

	~Movable() { MESSAGE("Movable destructor"); }
	Movable() { MESSAGE("Movable default constructor"); }
	Movable(int p_anything) : anything(p_anything), ptr(nullptr) { MESSAGE("Movable int constructor"); }
	Movable(int p_anything, void *p_ptr) : anything(p_anything), ptr(p_ptr) { MESSAGE("Movable full constructor"); }
	Movable(Movable &&other) noexcept : anything(other.anything), ptr(other.ptr) {
		MESSAGE("Movable move constructor");
		other.anything = 0;
		other.ptr = nullptr;
	}
	Movable &operator=(Movable &&other) noexcept {
		if (this != &other) {
			MESSAGE("Movable move assignment");
			anything = other.anything;
			ptr = other.ptr;
			other.anything = 0;
			other.ptr = nullptr;
		}
		return *this;
	}
	Movable(const Movable &other) : anything(other.anything), ptr(other.ptr) { MESSAGE("Movable copy constructor"); }
	Movable &operator=(const Movable &other) {
		if (this != &other) {
			MESSAGE("Movable copy assignment");
			anything = other.anything;
			ptr = other.ptr;
		}
		return *this;
	}
};

struct Slot {
	StringName name;
	Movable anything;

	Slot() = default;
	Slot(const StringName &p_name, int p_anything) : name(p_name), anything(p_anything) {}
	Slot(Slot &&other) noexcept = default;
	Slot &operator=(Slot &&other) noexcept = default;
	Slot(const Slot &other) = default;
	Slot &operator=(const Slot &other) = default;
};

TEST_CASE("[jsb.internal] SArray move_to_back") {
	internal::SArray<Movable> sarray;
	static constexpr int kCount = 10;
	for (int i = 0; i < kCount; ++i) {
		sarray.add(i);
	}
	CHECK(sarray.size() == kCount);
	for (int i = 0; i < 5; ++i) {
		CHECK(sarray.size() == kCount);
		internal::Index64 id = sarray.get_first_index();
		for (int p = rand() % (kCount / 2); p >= 0; --p) {
			id = sarray.get_next_index(id);
		}
		sarray.move_to_back(id);
		CHECK(sarray.get_last_index() == id);
		CHECK(sarray.size() == kCount);
	}
	internal::Index64 index = sarray.get_first_index();
	int dc = 0;
	while (index != internal::Index64::none()) {
		index = sarray.get_next_index(index);
		dc++;
	}
	CHECK(sarray.size() == dc);
}

TEST_CASE("[jsb.internal] SArray Movable test") {
	internal::SArray<Movable> sarray;
	MESSAGE("STEP ----");
	{
		Movable copy = 1;
		sarray.add(copy);
		copy.anything = -1;
	}
	MESSAGE("STEP ----");
	sarray.add(2);
	sarray.add(3);
	sarray.clear();
	MESSAGE("STEP ----");
	sarray.add(4);
}

TEST_CASE("[jsb.internal] SArray StringName test") {
	internal::SArray<Slot> sarray;
	{
		const StringName name = get_test_name();
		HashMap<StringName, Slot> map;
		map.insert(get_test_name(), { name, 0 });
		sarray.add({ get_test_name(), 1 });
	}
	{
		const StringName name = get_test_name();
		sarray.add({ get_test_name(), 1 });
		sarray.add({ name, 2 });
		sarray.clear();
	}
}
} //namespace jsb::tests

