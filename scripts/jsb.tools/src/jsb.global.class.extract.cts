/*
 *  jsb.global.class.extract.cts
 *
 *  This file is part of:
 *                                GodotJS-Ext
 *              https://github.com/Daylily-Zeleen/GodotJS-Ext
 *
 *  Copyright (c) 2026-present 忘忧の (Daylily-Zeleen)                  *
 *                 - Contact: daylily-zeleen@foxmail.com                *
 *
 *  This library is free software; you can redistribute it and/or       *
 *  modify it under the terms of the GNU Lesser General Public          *
 *  License as published by the Free Software Foundation; either        *
 *  version 2.1 of the License, or (at your option) any later version.  *
 *
 *  This library is distributed in the hope that it will be useful,     *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of      *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU    *
 *  Lesser General Public License for more details.                     *
 *                                                                      *
 *  You should have received a copy of the GNU Lesser General Public    *
 *  License along with this library; if not,                            *
 *  see <https://www.gnu.org/licenses/>.                                *
 */

/**
 * 全局类元数据提取（编辑器侧）：`GodotJSScriptLanguage::_get_global_class_name` 的输入。
 *
 * 用途一：编辑器的全局类缓存（`EditorFileSystem` 后台扫描 → `ProjectSettings` 的
 * `_global_script_class_cache`），字段为 `name / base_type / icon_path / is_tool / is_abstract`。
 * 用途二：`GodotJSScriptLanguage::is_global_class_generic`（场景类型化 codegen 用它判断
 * 该类是否带泛型参数），字段为 `is_generic`。两者同源同一次解析，故一起产出。
 * 此前由 C++ 侧正则提取；正则处理不了跨行、也会命中字符串/注释里的假文本，故改由本模块用
 * TS AST 提取（C++ 侧正则已删除）。
 *
 * 契约与旧正则一致，不扩大：
 *  - 取**被 default 导出**的那个类，支持三种形态：
 *      1. `export default class X extends Y`（.ts）
 *      2. `exports.default = class X extends Y`（.js）
 *      3. `class X extends Y` + `exports.default = X`（.js）
 *  - 必须显式 `extends`（旧正则各形态都要求），否则不产出。
 *  - `icon_path` 只认装饰器里的**字符串字面量**实参（`@bind.icon("res://…")` / `@icon(…)`）；
 *    变量实参视为未写（旧正则同样处理不了）。
 *  - 没有 default 导出 ⇒ `undefined`。
 */

import * as ts from "typescript";

import {
	decoratorsOf,
	decoratorName,
	extendsClauseText,
	findCommonJsDefaultClass,
	findDefaultClassByModifier,
	firstStringLiteralArg,
	isAbstract,
	parseSource,
	type AnyClass,
} from "./jsb.ts.ast.cjs";

export interface GlobalClassInfo {
	/** 引擎只认 `name`（见 `ScriptLanguageExtension::get_global_class_name`）。 */
	name: string;
	base_type: string;
	icon_path: string;
	is_tool: boolean;
	is_abstract: boolean;
	/** 默认导出的类是否带泛型参数（`class X<T>`）。 */
	is_generic: boolean;
}

/**
 * 找出源文件里被 default 导出的那个类。
 *
 * 只认两种形态（与旧 C++ 正则一致，不扩大）：
 *  1. `export default class X extends Y`（.ts 的常规写法）
 *  2. `exports.default = class X extends Y` / `exports.default = X`（.js 编译产物写法）
 *
 * 注意**不**接受 `export default X;`（标识符形态）：旧正则的 TS 分支要求
 * `export default class`，故这里保持同一约束。判据在 `jsb.ts.ast` 里是正交的三个函数，
 * 本处只组合其中需要的两个。
 */
function findDefaultClass(p_sourceFile: ts.SourceFile): AnyClass | undefined {
	return findDefaultClassByModifier(p_sourceFile) ?? findCommonJsDefaultClass(p_sourceFile);
}

/** 从一个已解析的源文件提取全局类元数据；没有 default 导出则 undefined。 */
export function collectGlobalClass(p_sourceFile: ts.SourceFile): GlobalClassInfo | undefined {
	const declared = findDefaultClass(p_sourceFile);
	if (declared === undefined || !declared.name) {
		return undefined;
	}

	const base_type = extendsClauseText(declared);
	// 旧正则各形态都要求显式 `extends`，这里保持同一约束，不顺手放宽。
	if (base_type === "") {
		return undefined;
	}

	let icon_path = "";
	let is_tool = false;
	for (const decorator of decoratorsOf(declared)) {
		// 装饰器短名取小写比较：`@bind.icon` 与 `@icon` 都算。
		switch (decoratorName(decorator)?.toLowerCase()) {
			case "icon":
				icon_path = firstStringLiteralArg(decorator);
				break;
			case "tool":
				is_tool = true;
				break;
			default:
				break;
		}
	}

	return {
		name: declared.name.text,
		base_type,
		icon_path,
		is_tool,
		is_abstract: isAbstract(declared),
		is_generic: (declared.typeParameters?.length ?? 0) > 0,
	};
}

/** 从源码文本提取；`p_fileName` 只用于推断 ScriptKind。 */
export function collectGlobalClassFromText(p_fileName: string, p_source: string): GlobalClassInfo | undefined {
	return collectGlobalClass(parseSource(p_fileName, p_source));
}
