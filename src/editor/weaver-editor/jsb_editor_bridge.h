/************************************************************************/
/*  jsb_editor_bridge.h                                                 */
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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU   */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once

// Editor-side client of the runtime bridge table (jsb_bridge_table.h).
//
// Locates the runtime's GodotJSScriptLanguage through the engine's public
// script language registry (Engine::get_script_language + get_class name
// check), fetches the bridge table address via the neutral `get_bridge`
// ClassDB method, and caches the resolved table.
//
// This file must NOT include anything from the runtime extension: the only
// contract is the JsbBridgeTable struct plus method-name strings.

#include <cstdint>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/script_language.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <internal/jsb_bridge_abi.h>

namespace jsb::editor {

class EditorBridge {
public:
	/// Resolve (and cache) the bridge table. Returns nullptr if the runtime
	/// extension is not loaded yet or the language is not initialized.
	static const JsbBridgeTable *get_bridge() {
		if (table_ != nullptr) {
			return table_;
		}
		godot::Engine *engine = godot::Engine::get_singleton();
		const int count = engine->get_script_language_count();
		for (int i = 0; i < count; i++) {
			godot::ScriptLanguage *lang = engine->get_script_language(i);
			if (lang == nullptr) {
				continue;
			}
			// Engine-level identification by registered class name; no
			// runtime types are involved on this side.
			if (lang->get_class() != "GodotJSScriptLanguage") {
				continue;
			}
			const godot::Variant address = lang->call("get_bridge");
			if (address.get_type() != godot::Variant::INT) {
				continue;
			}
			const auto *candidate = reinterpret_cast<const JsbBridgeTable *>((uint64_t)(int64_t)address);
			if (candidate->struct_size != sizeof(JsbBridgeTable) || candidate->eval == nullptr) {
				// Version mismatch; refuse rather than call into garbage.
				return nullptr;
			}
			table_ = candidate;
			return table_;
		}
		return nullptr;
	}

private:
	inline static const JsbBridgeTable *table_ = nullptr;
};

} //namespace jsb::editor
