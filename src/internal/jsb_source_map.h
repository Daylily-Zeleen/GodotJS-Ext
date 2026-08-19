/************************************************************************/
/*  jsb_source_map.h                                                    */
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

#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/string.hpp>

namespace jsb::internal {
struct IndexedSourcePosition {
	int index = 0;
	/** zero-based */
	int line = 0;
	/** zero-based */
	int column = 0;
};

struct SourcePosition {
	String function;
	String filename;
	int line = 0;
	int column = 0;
};

struct SourceMap {
private:
	struct InternalPosition {
		// DO NOT CHANGE THE ORDER
		int result_column = 0;
		int source_index = 0;
		int source_line = 0;
		int source_column = 0;
		int _reserved = 0; // name index

		_FORCE_INLINE_ int &operator[](uint8_t index) {
			jsb_check(index < 5);
			return *((int *)this + index);
		}
	};

	struct InternalLine {
		Vector<InternalPosition> elements;
		int result_line = 0;
	};

	Vector<InternalLine> source_lines_;
	Vector<String> sources_;
	String source_root_;

public:
	// input string is `mappings` (not the whole json), example: `;;;AAAA,iCAA6B;AAC7B,MAAa,QAAQ;CAAI`
	bool parse_mappings(const char *p_mappings, size_t p_len);

	// parse sourcemap json string
	bool parse(const String &p_source_map);

	// input: js source position [line, column]
	// output: ts source position
	//NOTE line & column are both zero-based （Source Map 规范）
	bool find(int p_line, int p_column, IndexedSourcePosition &r_pos) const;

	const String &get_source_root() const;
	const String &get_source(int index) const;

private:
	void decode(int p_line, const char *p_token, const char *p_end, InternalPosition &r_pos, int &r_aline, int &r_acol);
	InternalLine &operator[](int p_line);
};
} //namespace jsb::internal
