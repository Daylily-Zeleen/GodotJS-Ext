/************************************************************************/
/*  jsb_codegen_defs.h                                                  */
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

// jsb_codegen_defs.h
// Shared constants and trivial helpers for the C++ TypeScript declaration
// generator (P1 rewrite of scripts/jsb.editor/src/jsb.editor.codegen.ts).
//
// NOTE this module is editor-only (TOOLS_ENABLED).

#include <cstdint>

#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace jsb {
namespace codegen {

using namespace godot;

constexpr char kTab[] = "    ";
constexpr const char *kGodotAnyType = "GAny";
constexpr const char *kVersionDocsUrl = "https://docs.godotengine.org/en/latest";

// Mirror of DescriptorType in jsb_editor_helper.cpp (and formerly of
// jsb.editor.codegen.ts). Kept as a standalone enum so this module does not
// depend on editor helper internals.
enum class DescriptorType {
	Godot = 0,
	User,
	FunctionLiteral,
	ObjectLiteral,
	StringLiteral,
	NumericLiteral,
	BooleanLiteral,
	Union,
	Intersection,
	Conditional,
	Tuple,
	Infer,
	Mapped,
	Indexed,
};

// PredefinedLines (verbatim)
extern const char *const kPredefinedLines[];
extern const int kPredefinedLineCount;

// KeywordReplacement (verbatim, incl. the special `varargs` item)
extern const char *const kKeywordReplacements[][2];
extern const int kKeywordReplacementCount;

// Returns the replaced keyword or an empty String if `p_name` is not a keyword.
godot::String keyword_replacement(const godot::String &p_name);

// RemappedPrimitiveTypeNames
godot::String remapped_primitive_type_name(godot::Variant::Type p_type);

// name_string(): quote identifiers that collide with TS keywords or contain
// characters invalid in an identifier.
godot::String name_string(const godot::String &p_name);

// Formats an integral double the way JavaScript's Number.prototype.toString()
// does (shortest decimal that round-trips, fixed notation below 1e21). The old
// pipeline marshalled global constant values through v8 Numbers, so e.g.
// INT64_MIN rendered as -9223372036854776000 instead of the exact int64 value.
godot::String js_number_to_string(double p_value);

// makeCopyrightHeader()
godot::String make_copyright_header(const godot::String &p_filename);

// join_type_name(...): filters empty strings and joins with " | ".
godot::String join_type_name(const godot::Vector<godot::String> &p_parts);

// get_js_array_type_name(): `${element}[]`.
godot::String get_js_array_type_name(const godot::String &p_element_type_name);

} // namespace codegen
} // namespace jsb
