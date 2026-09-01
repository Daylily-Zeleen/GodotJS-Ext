
#pragma once

#if JSB_WITH_STATIC_BINDINGS

#include <cstdint>
#include <godot_cpp/variant/variant.hpp>

// forward declarations only -- pulling in the full v8 header here would leak
// engine-specific include paths into every translation unit including this one.
namespace v8 {
template <class T>
class FunctionCallbackInfo;
class Value;
} // namespace v8
#include <godot_cpp/variant/string_name.hpp>

namespace godot {
class StringName;
} // namespace godot

namespace jsb::static_binding {

using ThunkFn = void (*)(const v8::FunctionCallbackInfo<v8::Value> &);

// A single indexed property lookup yields BOTH accessor thunks: the getter
// and setter of one property always share the same (class, property) entry,
// so resolving them separately would run the class search twice.
struct IndexedPropertyThunks {
	ThunkFn getter = nullptr;
	ThunkFn setter = nullptr;
};

// Builtin hashes are computed from the SIGNATURE and are NOT unique within a
// type (e.g. String's casecmp_to family all share one hash), so the method
// name participates in the lookup. vt: GDExtensionVariantType value.
const ThunkFn find_builtin_thunk(godot::Variant::Type p_vt, const godot::StringName &p_name, uint32_t p_hash);

// Builtin member accessors (P3): p_vt is the base Variant type and p_name
// the member name (e.g. Vector2::"x"). Getter and setter have separate
// entries -- one accessor binds both.
const ThunkFn find_builtin_member_getter_thunk(godot::Variant::Type p_vt, const godot::StringName &p_name);
const ThunkFn find_builtin_member_setter_thunk(godot::Variant::Type p_vt, const godot::StringName &p_name);

// Same for utility functions.
const ThunkFn find_utility_thunk(const godot::StringName &p_name, uint32_t p_hash);

// Static operator path: the generated table covers every (left type,
// operator, right type) overload the api json defines for classes that have
// a JS surface. Unlisted combinations stay on the dynamic path.
const ThunkFn find_operator_thunk(godot::Variant::Type p_left, godot::Variant::Operator p_op, godot::Variant::Type p_right);

// Object-derived class methods: p_class is the engine class name
// (e.g. "Node"), p_name disambiguates same-hash overloads, hash is the
// official method hash.
const ThunkFn find_class_method_thunk(const godot::StringName &p_class,
        const godot::StringName &p_name, uint32_t p_hash);

// Indexed property accessors (P3): one thunk per (property side); the
// constant index lives on the accessor, not on the shared backing method.
// p_name is the PROPERTY name as exposed in the api json. Either side may be
// null when the api json does not provide the corresponding accessor method.
const IndexedPropertyThunks find_indexed_property_thunk(const godot::StringName &p_class,
        const godot::StringName &p_name);

} // namespace jsb::static_binding

#endif // JSB_WITH_STATIC_BINDINGS
