/************************************************************************/
/*  type_compatible.h                                                   */
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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU    */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

/**
 * @brief 静态绑定内置类型构造函数参数类型兼容性检查
 *
 * 本地允许源类型集合，用于构造函数重载筛选，不执行实际转换。
 * 编译期目标类型 TargetT + 运行期源类型 p_source_type -> bool。
 *
 * 用法：can_be_converted_from<godot::Variant::VECTOR2>(argts[i])
 *
 * ## 契约（改本表前必读）
 *
 * 本谓词与"实际编组器"是同一决策的两半：谓词在生成期选出重载
 * （`find_ctor_<T>` 里的 `can_be_converted_from<...>(argts[i])`），
 * 编组器在运行期真正转换（`marshal_one<CppT>` -> `try_js_to_gd` ->
 * `JSToGD<CppT>::convert`，`jsb_type_convert_direct.h`）。
 *
 * **谓词的接受面必须 ⊆ 对应 C++ 形参编组器的接受面。**
 * 违反即"选中某重载后被 marshal 拒绝"，表现为 `bad argument N`
 * （历史实例：`COLOR` 曾放行 `STRING`/`INT`，而 `JSToGD<godot::Color>`
 * 只接受 Variant 包装对象，导致 `new Color("abc")` 错选 `Color(Color)`
 * 后编组失败；see bench `Constructors` 组）。
 *
 * 注意两侧判据**不是一回事**：谓词只看 `probe_vt` 得到的 Variant 类型对，
 * 编组器看 JS 值的实际形态（`IsString` / `IsNumber` / `IsObject && is_variant`）。
 * 因此标量源（STRING/INT/...）只能放行给**确实接受该标量的编组器**
 * （`JSToGD<String>` 等），不能放行给仅接受包装对象的编组器。
 *
 * ## 审计表（新增/修改目标类型时逐行核对）
 *
 * | 目标 | 接受的额外源 | 该目标的编组器 | 是否一致 |
 * |---|---|---|---|
 * | `COLOR` | （无） | `extract_variant_backed`（仅包装） | ✓ |
 * | `RID` | （无） | `extract_variant_backed`（仅包装） | ✓ |
 * | `STRING_NAME` | `STRING` | `JSToGD<StringName>`（接受 `IsString`） | ✓ |
 * | `NODE_PATH` | `STRING` | `JSToGD<NodePath>`（接受 `IsString`） | ✓ |
 * | `BOOL` | `INT` / `FLOAT` / `NIL` | `JSToGD<bool>` → `Helper::to_bool`（boolean / number / bigint / null / undefined） | ✓ |
 * | `INT` | `BOOL` / `FLOAT` / `NIL` | `JSToGD<int64_t>` → `to_int64` + `js_bool_as_number`（boolean / number / bigint） | ✓ |
 * | `FLOAT` | `BOOL` / `INT` / `NIL` | `JSToGD<float>` / `<double>` → `to_double` + `js_bool_as_number` | ✓ |
 * | `ARRAY` / `PACKED_*_ARRAY` | 双向 | `extract_variant_backed` + 容器回退 | ✓ |
 * | 向量/矩形/变换家族 | 同类互转 | `extract_variant_backed`（仅包装） | ✓ |
 * | `OBJECT` | （无） | `JSToGD<Object *>`（`is_object`） | ✓ |
 *
 * 数值三行的口径与引擎 `Variant::can_convert_strict` 对齐
 * （`core/variant/variant.cpp`：`BOOL = {INT, FLOAT, NIL}`、`INT = {BOOL, FLOAT, NIL}`、
 * `FLOAT = {BOOL, INT, NIL}`，三处的 `STRING` 都被注释掉）。
 * `NIL` 由本函数开头的 `p_source_type == Variant::NIL` 统一放行，故三行都含它。
 * **`BIGINT` 不是一个 Variant 类型**：`probe_vt` 把 JS BigInt 归为 `Variant::INT`
 * （见 `thunks_common.h` 的 `probe_vt`），所以 BigInt 走的是 `INT` 那一列 ——
 * 对 `INT` 目标是同类型命中，对 `FLOAT` 目标靠 `INT` 这一行放行。
 *
 * 数值三行的一致性由 `js_bool_as_number`（`jsb_type_convert_direct.h`）保证：
 * 引擎的 `INT` / `FLOAT` 都接受 `BOOL`，所以 `JSToGD<int64_t>` / `<float>` / `<double>`
 * 也必须接受 boolean，否则谓词选中数值重载而编组器随后拒绝。
 *
 * 向量/变换家族之所以一致：其"额外源"本身都是可包装的 Variant 类型，
 * 而编组器接受任意包装对象——类型不匹配由引擎 ctor 自身拒绝。
 */

#pragma once

#include <godot_cpp/variant/variant.hpp>

namespace jsb::static_binding {

// ---------------------------------------------------------------------------
// 核心检查：编译期目标类型 + 运行期源类型
template <godot::Variant::Type TargetT>
_FORCE_INLINE_ bool can_be_converted_from(godot::Variant::Type p_source_type) {
	if (p_source_type == TargetT) return true;

	// 重载筛选将 NIL 视为任意目标类型的候选。
	if (p_source_type == godot::Variant::NIL) return true;

	switch (TargetT) {
		// 基础数值互转
		case godot::Variant::BOOL:
			return p_source_type == godot::Variant::INT || p_source_type == godot::Variant::FLOAT;

		case godot::Variant::INT:
			return p_source_type == godot::Variant::BOOL || p_source_type == godot::Variant::FLOAT;

		case godot::Variant::FLOAT:
			return p_source_type == godot::Variant::BOOL || p_source_type == godot::Variant::INT;

		// 字符串相关
		case godot::Variant::STRING:
			return p_source_type == godot::Variant::NODE_PATH || p_source_type == godot::Variant::STRING_NAME;

		case godot::Variant::STRING_NAME:
			return p_source_type == godot::Variant::STRING;

		case godot::Variant::NODE_PATH:
			return p_source_type == godot::Variant::STRING;

		// 向量/矩形整数与浮点类型互转
		case godot::Variant::VECTOR2:
			return p_source_type == godot::Variant::VECTOR2I;
		case godot::Variant::VECTOR2I:
			return p_source_type == godot::Variant::VECTOR2;

		case godot::Variant::VECTOR3:
			return p_source_type == godot::Variant::VECTOR3I;
		case godot::Variant::VECTOR3I:
			return p_source_type == godot::Variant::VECTOR3;

		case godot::Variant::VECTOR4:
			return p_source_type == godot::Variant::VECTOR4I;
		case godot::Variant::VECTOR4I:
			return p_source_type == godot::Variant::VECTOR4;

		case godot::Variant::RECT2:
			return p_source_type == godot::Variant::RECT2I;
		case godot::Variant::RECT2I:
			return p_source_type == godot::Variant::RECT2;

		// 变换矩阵
		case godot::Variant::TRANSFORM2D:
			return p_source_type == godot::Variant::TRANSFORM3D;
		case godot::Variant::TRANSFORM3D:
			return p_source_type == godot::Variant::TRANSFORM2D
					|| p_source_type == godot::Variant::QUATERNION
					|| p_source_type == godot::Variant::BASIS
					|| p_source_type == godot::Variant::PROJECTION;

		case godot::Variant::PROJECTION:
			return p_source_type == godot::Variant::TRANSFORM3D;

		// QUATERNION <-> BASIS
		case godot::Variant::QUATERNION:
			return p_source_type == godot::Variant::BASIS;
		case godot::Variant::BASIS:
			return p_source_type == godot::Variant::QUATERNION;

		// COLOR / RID: no extra scalar sources. Their ctor marshaller is
		// extract_variant_backed (jsb_type_convert_direct.h), which accepts only
		// a Variant-backed wrapper -- never a raw JS string/number/object.
		// Letting STRING/INT through here made `new Color("abc")` select
		// Color(Color) and then fail in marshal_one ("bad argument 0").
		// The scalar constructors live on the String overload, selected by the
		// STRING predicate once this one declines.
		case godot::Variant::COLOR:
		case godot::Variant::RID:
			return false;

		// OBJECT 仅接受同类型或 NIL（均已在开头处理）。

		// 数组类型
		case godot::Variant::ARRAY: {
			// ARRAY 接受所有 PACKED_*_ARRAY
			return p_source_type >= godot::Variant::PACKED_BYTE_ARRAY
					&& p_source_type <= godot::Variant::PACKED_VECTOR4_ARRAY;
		}

		// 反向：每个 PACKED_*_ARRAY 接受 ARRAY
		case godot::Variant::PACKED_BYTE_ARRAY:
		case godot::Variant::PACKED_INT32_ARRAY:
		case godot::Variant::PACKED_INT64_ARRAY:
		case godot::Variant::PACKED_FLOAT32_ARRAY:
		case godot::Variant::PACKED_FLOAT64_ARRAY:
		case godot::Variant::PACKED_STRING_ARRAY:
		case godot::Variant::PACKED_COLOR_ARRAY:
		case godot::Variant::PACKED_VECTOR2_ARRAY:
		case godot::Variant::PACKED_VECTOR3_ARRAY:
		case godot::Variant::PACKED_VECTOR4_ARRAY:
			return p_source_type == godot::Variant::ARRAY;

		default:
			return false;
	}
}

} // namespace jsb::static_binding