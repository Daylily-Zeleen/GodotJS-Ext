/************************************************************************/
/*  builtin_constructors.h                                              */
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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the        */
/*  GNU Lesser General Public License for more details.                 */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once

#if JSB_WITH_STATIC_BINDINGS

#	include "thunks_common.h"
#	include "type_compatible.h"

#	include <gdextension_interface.h>
#	include <godot_cpp/core/builtin_ptrcall.hpp>
#	include <godot_cpp/variant/variant_internal.hpp>

namespace jsb::static_binding::thunks {

namespace internal {

// Include the target, argument count and probed types when no overload matches.
// `inline`, not `static`: this header is included by several TUs, and `static`
// would mint an unused internal-linkage copy in each one.
inline void throw_no_suitable_ctor(
		godot::Variant::Type p_target, const v8::FunctionCallbackInfo<v8::Value> &info) {
	godot::String detail;
	for (int i = 0; i < info.Length(); ++i) {
		const Variant::Type vt = probe_vt(info[i]);
		if (!detail.is_empty()) detail += ", ";
		detail += vt == Variant::VARIANT_MAX
				? godot::String("(unknown)")
				: static_cast<godot::String>(godot::Variant::get_type_name(vt));
	}
	jsb_throw(info.GetIsolate(), jsb_errorf("no suitable constructor for %s (received %d arg(s): %s)", godot::Variant::get_type_name(p_target), info.Length(), detail));
}

} // namespace internal

// Per-overload constructor thunk with exact arity. Resolve the engine ctor
// by (VTC, CtorIndex), marshal typed ptrcall arguments, and construct into
// uninitialized TargetCppT storage before copying into the bound Variant.
template <godot::Variant::Type VTC, int32_t CtorIndex, class AllArgsT>
void builtin_ctor_thunk(const v8::FunctionCallbackInfo<v8::Value> &info) {
	using TargetCppT = VariantNativeType_t<VTC>;
	using AllArgsTuple = typename AllArgsT::tuple;
	v8::Isolate *isolate = info.GetIsolate();
	const v8::Local<v8::Context> context = isolate->GetCurrentContext();

	static const GDExtensionPtrConstructor ctor = ::godot::gdextension_interface::variant_get_ptr_constructor(
			(GDExtensionVariantType)VTC, CtorIndex);
	if (!ctor) {
		jsb_throw(isolate, jsb_errorf("static binding: failed to load builtin constructor %s (index %d): missing builtin constructor", godot::Variant::get_type_name(VTC), CtorIndex));
		return;
	}

	constexpr int N = (int)std::tuple_size_v<AllArgsTuple>;
	if (info.Length() != N) {
		jsb_throw(isolate, jsb_errorf("num of arguments does not meet the requirement: %s constructor expects %d, got %d", godot::Variant::get_type_name(VTC), N, (int)info.Length()));
		return;
	}

	// Typed EncodeT slots remain alive through the constructor call.
	typename AllArgsT::encode_slots slots;
	bool ok = true;
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		(void)((ok = marshal_one<std::tuple_element_t<I, AllArgsTuple>>(isolate, context, info, (int)I, std::get<I>(slots), N)) && ...);
	}(std::make_index_sequence<N>{});
	if (!ok) {
		return; // marshal_one already jsb_threw
	}

	void *arg_ptrs[N > 0 ? N : 1] = {};
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		((void)(arg_ptrs[I] = (void *)&std::get<I>(slots)), ...);
	}(std::make_index_sequence<N>{});

	// ctor requires uninitialized native storage, not a NIL Variant: it does
	// not set a Variant type tag. Copy the constructed value into a Variant
	// before destroying the temporary native value.
	std::aligned_storage_t<sizeof(TargetCppT), alignof(TargetCppT)> base_storage;
	ctor(&base_storage, arg_ptrs);
	const TargetCppT *constructed = reinterpret_cast<const TargetCppT *>(&base_storage);

	Environment *env = Environment::wrap(isolate);
	Variant *instance = env->alloc_variant();
	*instance = std::move(*constructed);

	constructed->~TargetCppT();

	env->bind_valuetype(instance, info.This());
}

} // namespace jsb::static_binding::thunks

#endif // JSB_WITH_STATIC_BINDINGS
