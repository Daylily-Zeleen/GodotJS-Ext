
#pragma once

#if JSB_WITH_STATIC_BINDINGS

#include <cstdint>

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

// Builtin hashes are computed from the SIGNATURE and are NOT unique within a
// type (e.g. String's casecmp_to family all share one hash), so the method
// name participates in the lookup. vt: GDExtensionVariantType value.
const ThunkFn find_builtin_thunk(uint32_t p_vt, const godot::StringName &p_name, uint64_t p_hash);

// Same for utility functions.
const ThunkFn find_utility_thunk(const godot::StringName &p_name, uint64_t p_hash);

} // namespace jsb::static_binding

#endif // JSB_WITH_STATIC_BINDINGS
