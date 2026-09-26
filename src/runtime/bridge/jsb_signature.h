/************************************************************************/
/*  jsb_signature.h                                                     */
/*                                                                      */
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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU    */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once

#include "jsb_class_info.h"

namespace jsb::internal {

/**
 * 读取一个函数/信号签名清单（sidecar）。
 *
 * 清单与编译产物同名同目录，只换扩展名（`<outDir>/foo.js` -> `<outDir>/foo.sig`），
 * 由编辑器侧的 Node 提取器写出（`scripts/jsb.editor/src/signature/jsb.signature.extract.cts`）。
 * 格式、魔数与版本校验见 `.trellis/tasks/09-26-method-signal-signature/design.md` §4。
 *
 * @param p_module_id 编译产物（`.js`）的 `res://` 路径，与 `script_class_info_.module_id` 一致
 * @param r_methods   方法名 -> 按源码声明顺序的全部签名（无重载时长度为 1）
 * @param r_signals   信号名 -> 参数表（信号恒为单签名、返回值恒 void，故只取参数）
 * @return 清单存在且魔数/版本合法时为 true；不存在/损坏/版本不符时为 false
 *
 * @note 返回 false 是**常态而非异常**：没有类成员的脚本不产出清单，调用方应静默退回既有行为，
 *       不报错、不产生成员。
 */
bool signature_load(const StringName &p_module_id,
		HashMap<StringName, LocalVector<ScriptMethodSignature>> &r_methods,
		HashMap<StringName, LocalVector<PropertyInfo>> &r_signals);

/**
 * 把一个清单里的类型名映射成 `PropertyInfo`（design.md §5.2）。
 *
 * **Godot 侧的全部类型知识都在本函数**：提取器只输出类型名字符串，避免 JS/C++ 两份类型表漂移。
 * 映射顺序：
 *  1. 引擎别名（`int32` / `StringName` / …）—— 它们在 AST 上保留原文，在 checker 上会退化成
 *     `number` / `string`，所以只能按原文识别；
 *  2. `get_original_name()` 反查**原始类名** —— JS 侧暴露名不一定等于 Godot 原始名
 *     （`GArray` -> `Array`、`GDictionary` -> `Dictionary`，camel_case 下还有更多），
 *     不先反查就拿暴露名查表会命中不了，`class_exists` 也会误判成未知；
 *  3. `Variant::get_type_name` 反向表（现构，不硬编码，引擎加类型自动跟上）；
 *  4. `ClassDB::class_exists` -> `Variant::OBJECT` + `class_name`（仍用**原始名**，
 *     inspector 按原始类名显示）；
 *  5. 仍未命中 -> `Variant::NIL`。
 *
 * @param p_is_argument 参数位（true）还是返回值位（false）。**两种位置的 NIL 语义不同**：
 *        参数位的 NIL 一律被 GDScript 解释为 VARIANT（任意值）；返回值位只有带
 *        `PROPERTY_USAGE_NIL_IS_VARIANT` 才表示"任意值"，不带则表示**真 `void`**
 *        （取它的返回值会报错）。这两种语义不能只用一个 NIL 表达。
 */
PropertyInfo signature_resolve_type(const String &p_type_name, const StringName &p_name, bool p_is_argument);

/**
 * 从函数源文本数出**已声明参数个数**（剩余参数不计）。
 *
 * 为什么不用 `Function.length`：它是下界而非个数 —— 遇到第一个带默认值的参数或剩余参数就停，
 * `(a, b = 2, ...rest)` 报 1 而不是 3；且各 JS 腿对 `v8::Function` 是否暴露 `Length()` 并不一致。
 *
 * 返回 `ScriptArgumentCount::Unknown` 表示**读不出来**（形参表闭合不了），调用方必须按"无效"处理，
 * **不得当成 0**。两个具名盲点：① 正则字面量里的不平衡括号（区分正则与除法需要完整文法）；
 * ② V8 把超过 128 字符的函数源文本截断为前 111 字符 + 省略标记。
 */
int count_declared_parameters(const String &p_source);

/**
 * 把一个清单签名转成引擎的 `MethodInfo`（design.md §6 的三态编码在这里落地）。
 *
 *  - 可选参数 -> `default_arguments` 追加占位 `Variant()`（取不到 TS 默认值表达式的值，只有个数
 *    有意义 —— 它决定 GDScript 的最小 arity）；
 *  - 剩余参数 -> `flags |= METHOD_FLAG_VARARG`（GDScript 只在非 VARARG 时检查实参上限）；
 *  - 返回值 -> 原样搬运 `return_val`，真 `void` 与"不可映射"的区别由它的 usage 位承载。
 */
MethodInfo signature_to_method_info(const StringName &p_name, const ScriptMethodSignature &p_signature);

/**
 * **没有签名清单时的回退**：在给定环境里按需取该方法的函数对象（复用脚本类信息里的
 * `method_cache`，与 `Environment::call_script_method` 同机制），读出源文本并数已声明参数个数。
 *
 * 之所以要回退而不是直接报"未知"：清单是**编辑器侧构建产物**，纯 JS 项目、只跑运行时的导出包、
 * 或尚未跑过提取器的工程里根本没有它 —— 那些场景下"参数个数"仍应是可用的（与改动前一致），
 * 不能因为多了一层清单就退化成"读不出来"。
 *
 * @return `ScriptArgumentCount::Unknown` 表示无法判定（模块/类/方法缺失，或形参表闭合不了）。
 */
int resolve_declared_parameter_count(Environment *p_env, const StringName &p_module_id, const StringName &p_exposed_name);

} //namespace jsb::internal
