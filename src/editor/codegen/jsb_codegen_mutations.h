/************************************************************************/
/*  jsb_codegen_mutations.h                                             */
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

// jsb_codegen_mutations.h
// Port of TypeMutations / InheritedTypeMutations from
// scripts/jsb.editor/src/jsb.editor.codegen.ts plus the line-mutator helpers
// (mutate_parameter_type / mutate_return_type / mutate_template).
//
// A mutator rewrites one emitted declaration line with std::regex, mirroring
// the TS implementation. Overrides are either literal replacement lines or a
// mutator applied to the originally generated line.

#include "jsb_codegen_defs.h"

#include <functional>
#include <memory>

#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/templates/pair.hpp>
#include <godot_cpp/templates/vector.hpp>

namespace jsb {
namespace codegen {

class TypeDB;

// line -> line
using LineMutator = std::function<String(const String &)>;

LineMutator make_chain_mutator(Vector<LineMutator> p_mutators);
LineMutator make_mutate_parameter_type(const String &p_name, const String &p_type);
LineMutator make_mutate_return_type(const String &p_type);
LineMutator make_mutate_template(const String &p_template);

struct GenericParameter {
	String extends_;
	String default_;
};

struct ImplementsEntry {
	String type;
	Vector<String> generic_arguments;
};

struct PropertyOverride {
	bool is_literal = false;
	Vector<String> literal_lines; // valid when is_literal
	LineMutator mutator; // valid when !is_literal
};

struct TypeMutation {
	// insertion ordered
	LocalVector<Pair<String, GenericParameter>> generic_parameters;

	String super; // empty = not overridden
	Vector<String> super_generic_arguments;
	LocalVector<ImplementsEntry> implements;

	Vector<String> intro;
	Vector<String> prelude;

	HashMap<String, PropertyOverride> property_overrides;

	_FORCE_INLINE_ bool is_empty() const {
		return generic_parameters.is_empty() && super.is_empty() && super_generic_arguments.is_empty()
				&& implements.is_empty() && intro.is_empty() && prelude.is_empty() && property_overrides.is_empty();
	}
};

// merge_type_mutations(): overrides win; literal overrides replace, mutators compose.
TypeMutation merge_type_mutations(const TypeMutation &p_base, const TypeMutation &p_overrides);

// get_type_mutation(name, classes): walks ancestors applying inherited tables.
// p_apply_intrinsic mirrors the TS call-shape difference: emit_godot_class passes
// `this._types.classes` (classes only), while emit_godot_primitive calls
// get_type_mutation(type_name) WITHOUT the classes map - so primitive intrinsic
// intros (set_indexed/get_indexed/...) never fire in the old pipeline.
TypeMutation get_type_mutation(const String &p_name, const TypeDB *p_types, bool p_apply_intrinsic = true);

// --- table access (defined in jsb_codegen_mutations.cpp) ---
// direct per-class table (TypeMutations); nullptr when the class has no entry
const TypeMutation *find_direct_mutation(const String &p_name);
// inherited table (InheritedTypeMutations); nullptr when absent
const TypeMutation *find_inherited_mutation(const String &p_name);
// intrinsic mutation derived from the class decl itself (keyed/indexed intro)
TypeMutation build_intrinsic_mutation(const String &p_name, const TypeDB *p_types);

} // namespace codegen
} // namespace jsb
