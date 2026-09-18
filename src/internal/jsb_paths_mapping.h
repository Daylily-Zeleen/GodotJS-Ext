/************************************************************************/
/*  jsb_bridge_abi.h                                                    */
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

#pragma once

#include <godot_cpp/variant/string.hpp>

namespace jsb {

/**
 * @brief TypeScript 路径映射（tsconfig.json paths）统一管理
 *
 * 文件格式（.paths_mapping，与编译产物同目录，TS paths 完整语义的线性化）：
 *   每行 "has_star<TAB>pattern前缀<TAB>pattern后缀<TAB>候选1前缀<TAB>候选1后缀<TAB>候选2前缀<TAB>候选2后缀..."
 *   '#' 开头为注释行。
 *   has_star=0 时 pattern 为精确匹配（整串全等，忽略后缀字段）；
 *   has_star=1 时通配符匹配 = begins_with(前缀) && ends_with(后缀)，
 *   '*' 在末尾时后缀为空。候选替换 = 候选前缀 + 捕获 + 候选后缀
 *   （候选后缀为空 = 无 '*'，捕获直接拼接；TS 官方 sub.replace 语义）。
 *
 * 此头文件位于公共区域（编入两个库），内部实现按
 * JSB_EDITOR_LIB_BUILD / JSB_RUNTIME_LIB_BUILD 区分 Editor/Runtime 侧。
 * 调用方以 #if JSB_USE_TYPESCRIPT 条件引入与调用。
 */
class PathsMapping {
public:
#if JSB_RUNTIME_LIB_BUILD
	// 从 p_file_path 加载映射并解析（拆解 pattern/候选）；文件缺失或格式错误
	// 返回 false。带参版供单测注入假数据；无参版从项目标准路径加载。
	static bool refresh_from(const godot::String &p_file_path);

	// 从项目标准路径（与编译产物同目录）重新加载；文件缺失时清空缓存并返回 false
	static bool refresh();

	// 按 pattern 匹配导入路径（前缀长度降序，最先命中者胜）。
	// pattern 含 '*'：匹配 = begins_with(前缀) && ends_with(后缀)，捕获为中间部分；
	// pattern 无 '*'：全等匹配。
	// 命中后按 tsconfig 声明顺序对每个候选执行替换（候选首个 '*' 由捕获替换；
	// 无 '*' 则捕获丢弃），并立即调用 p_try_resolve（参数为替换后的模块 ID）；
	// 返回 true 表示解析成功，停止。全部失败/未命中返回 false，
	// 调用方按原 ID 继续常规解析。拼一个试一个，无中间容器分配。
	using CandidateTryFn = bool (*)(void *p_userdata, const godot::String &p_candidate_id);
	static bool resolve(const godot::String &p_import_path, void *p_userdata, CandidateTryFn p_try_resolve);
#endif

#if JSB_EDITOR_LIB_BUILD
	// 解析 tsconfig.json 内容并写入 .paths_mapping（无 paths 配置时删除旧文件）
	static bool generate_from_tsconfig(const godot::String &p_jsonc_content);

	// .paths_mapping 文件路径（供导出插件打包该文件）
	static godot::String get_paths_mapping_file_path();
#endif
};

} // namespace jsb
