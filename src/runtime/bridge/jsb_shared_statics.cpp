/************************************************************************/
/*  jsb_shared_statics.cpp                                              */
/************************************************************************/
/*  This file is part of:                                               */
/*                                GodotJS-Ext                           */
/*              https://github.com/Daylily-Zeleen/GodotJS-Ext           */
/*                                                                      */
/*  Copyright (c) 2026-present 忘忧の (Daylily-Zeleen)                  */
/*                 - Contact: daylily-zeleen@foxmail.com                */
/*  Copyright (c) Contributors of GodotJS                               */
/*                 <https://github.com/godotjs/GodotJS>                 */
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

#include "jsb_shared_statics.h"

#include <mutex>

namespace jsb {
namespace {
typedef HashMap<StringName, HashMap<StringName, Variant>> SharedStaticStore;

// Function-local statics keep the storage out of the header, so the two extensions do not each get
// a copy (the runtime DLL owns it; the editor DLL does not touch static variables at all).
//
// Locking: this mutex is a leaf. No call below re-enters the JS runtime or the language, so a
// caller may hold other locks while taking this one and the pair stays deadlock-free. The
// converse does not hold and is not relied upon: the parse path
// (`load_module_immediately` -> `_parse_script_class_iterate`) does not take
// `GodotJSScriptLanguage::mutex_`, and the rebind loop that does take it
// (`jsb_script.cpp`, the `SharedStatics` accessor rebind) reaches this lock only through
// `SharedStatics::get`/`set`/`ensure`, never the other way around.
std::recursive_mutex &_mutex() {
	static std::recursive_mutex mutex;
	return mutex;
}

SharedStaticStore &_storage() {
	static SharedStaticStore storage;
	return storage;
}
} // namespace

void SharedStatics::ensure(const StringName &p_module_id, const StringName &p_name, const Variant &p_initial_value) {
	std::lock_guard lock(_mutex());
	HashMap<StringName, Variant> &entries = _storage()[p_module_id];
	if (!entries.has(p_name)) {
		entries.insert(p_name, p_initial_value);
	}
}

bool SharedStatics::get(const StringName &p_module_id, const StringName &p_name, Variant &r_value) {
	std::lock_guard lock(_mutex());
	const SharedStaticStore::ConstIterator it = _storage().find(p_module_id);
	if (!it) {
		return false;
	}
	const HashMap<StringName, Variant>::ConstIterator entry = it->value.find(p_name);
	if (!entry) {
		return false;
	}
	r_value = entry->value;
	return true;
}

bool SharedStatics::set(const StringName &p_module_id, const StringName &p_name, const Variant &p_value) {
	std::lock_guard lock(_mutex());
	const SharedStaticStore::Iterator it = _storage().find(p_module_id);
	if (!it) {
		return false;
	}
	HashMap<StringName, Variant>::Iterator entry = it->value.find(p_name);
	if (!entry) {
		return false;
	}
	entry->value = p_value;
	return true;
}

void SharedStatics::retain(const StringName &p_module_id, const HashSet<StringName> &p_names) {
	std::lock_guard lock(_mutex());
	const SharedStaticStore::Iterator it = _storage().find(p_module_id);
	if (!it) {
		return;
	}
	HashMap<StringName, Variant> &entries = it->value;
	List<StringName> dropped;
	for (const KeyValue<StringName, Variant> &entry : entries) {
		if (!p_names.has(entry.key)) {
			dropped.push_back(entry.key);
		}
	}
	for (const StringName &name : dropped) {
		entries.erase(name);
	}
	if (entries.is_empty()) {
		_storage().erase(p_module_id);
	}
}

void SharedStatics::clear() {
	std::lock_guard lock(_mutex());
	_storage().clear();
}
} // namespace jsb
