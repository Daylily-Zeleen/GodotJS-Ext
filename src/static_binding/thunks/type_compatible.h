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

		case godot::Variant::COLOR:
			return p_source_type == godot::Variant::STRING || p_source_type == godot::Variant::INT;

		case godot::Variant::RID:
			return p_source_type == godot::Variant::OBJECT;

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