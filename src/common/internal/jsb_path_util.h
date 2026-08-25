/************************************************************************/
/*  jsb_path_util.h                                                     */
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

#include <runtime/compat/jsb_compat.h>
#include <godot_cpp/variant/string.hpp>

namespace jsb::internal {
template <typename T>
concept GodotString = std::is_convertible_v<T, String>;

class PathUtil {
public:
	static _FORCE_INLINE_ String combine(const String &p_base, const String &p_add) {
		return p_base.path_join(p_add);
	}

	template <GodotString... Component>
	static _FORCE_INLINE_ String combine(const String &p_base, const String &p_add, const Component &...p_remain) {
		return combine(p_base.path_join(p_add), p_remain...);
	}

	/**
	 * @brief convert the given path into a platform specific (filesystem) path.
	 * @note input types are NOT probed by opening the file. The path is normalized first, then
	 *       godot virtual paths (`res://`, `user://`, `uid://`) are globalized via
	 *       `ProjectSettings::globalize_path()`. The platform path is only returned when the
	 *       globalized file actually exists on disk; otherwise the original normalized path is
	 *       kept (files inside a packed PCK have no meaningful filesystem location, and a
	 *       `res://` path is still resolvable inside the Godot ecosystem).
	 */
	static String to_platform_specific_path(const String &p_path);

	// return the upper directory path ('/' and '\\' are both accepted)
	// protocol roots (`res://`, `user://`), windows drives and the posix root are preserved
	// (aligned with `godot::String::get_base_dir()`)
	static _FORCE_INLINE_ String dirname(const String &p_name) { return p_name.get_base_dir(); }

	// return the last component of the give path ('/' and '\\' are both accepted)
	// (aligned with `godot::String::get_file()`)
	static _FORCE_INLINE_ String get_last_component(const String &p_name) { return p_name.get_file(); }

	/**
	 * @brief normalize the given path: resolve `.` and `..`, collapse duplicate separators and
	 *        convert `\` into `/`. Protocol roots (`res://`, `user://`) are preserved.
	 * @note `..` above the protocol root is NOT clamped (pure string normalization); whether the
	 *       result is a legal path is up to the caller.
	 */
	static _FORCE_INLINE_ Error extract(const String &p_path, String &o_path) {
		o_path = p_path.simplify_path();
		return OK;
	}

	static String extends_with(const String &p_path, const String &p_ext);

	/**
	 * @brief if the given path string is absolute (godot virtual path, windows drive/UNC, or posix root)
	 */
	static _FORCE_INLINE_ bool is_absolute_path(const String &p_path) { return p_path.is_absolute_path(); };

	/**
	 * convert typescript path into javascript path
	 * (project-relative mapping: `res://<rel>.ts` -> `<outDir>/<rel>.js`)
	 */
	static String convert_typescript_path(const String &p_source_path);

	/**
	 * convert javascript path back into typescript path
	 * (project-relative mapping: `<outDir>/<rel>.js` -> `res://<rel>.ts`, boundary-aware)
	 */
	static String convert_javascript_path(const String &p_source_path);

	/** simply verify the file extension (.js || .cjs || .mjs) */
	static bool is_recognized_javascript_extension(const String &p_path);

	/**
	 * delete a file (or an empty directory). Accepts `res://`/`user://` virtual paths as well as
	 * os filesystem paths (implemented via `DirAccess::remove_absolute()`).
	 */
	static bool delete_file(const String &p_path);
};

} //namespace jsb::internal
