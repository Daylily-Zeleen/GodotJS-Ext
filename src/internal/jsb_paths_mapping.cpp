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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU   */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#include "jsb_paths_mapping.h"
#include "jsb_macros.h" // jsb.config.h + jsb.gen.h (JSB_USE_TYPESCRIPT 等编译开关)

#if JSB_USE_TYPESCRIPT

#	include <godot_cpp/classes/dir_access.hpp>
#	include <godot_cpp/classes/file_access.hpp>
#	include <godot_cpp/classes/json.hpp>
#	include <godot_cpp/templates/pair.hpp>
#	include <godot_cpp/templates/vector.hpp>

#	include <internal/jsb_logger.h>
#	include <internal/jsb_path_util.h>
#	include <internal/jsb_settings.h>

namespace jsb {

namespace {

// 一个 paths 条目的完整线性化：pattern 与候选都拆为 '*' 前/后缀。
// pattern 无 '*'（has_star=false）= 精确匹配；'*' 在末尾时后缀为空但仍是
// 通配符匹配（捕获任意非空后缀）。
// 候选替换（TS 官方 sub.replace("*", captured) 语义）：
//   候选有 '*'：结果 = 候选前缀 + 捕获 + 候选后缀
//   候选无 '*'：捕获丢弃，结果 = 候选整串
struct PathsCandidate {
	bool has_star = false;
	godot::String prefix;
	godot::String suffix; // 仅 has_star 时有效
};

struct PathsEntry {
	bool has_star = false;
	godot::String pattern_prefix;
	godot::String pattern_suffix; // 仅 has_star 时有效（'*' 在末尾时为空）
	godot::Vector<PathsCandidate> candidates;
};

#	if JSB_RUNTIME_LIB_BUILD
// 文件内缓存：运行时侧已加载的映射表（pattern 前缀长度降序，最长优先）
godot::Vector<PathsEntry> s_mappings;

// pattern 前缀长度降序（最长前缀优先匹配）
struct PathsEntryComparator {
	bool operator()(const PathsEntry &a, const PathsEntry &b) const {
		return a.pattern_prefix.length() > b.pattern_prefix.length();
	}
};
#	endif

// .paths_mapping 与编译产物（jsb::internal::settings::get_jsb_out_res_path，默认 res://.godot/godotjs_ext）同目录
godot::String get_paths_mapping_path() {
	return jsb::internal::settings::get_jsb_out_res_path().path_join(".paths_mapping");
}
#	if JSB_EDITOR_LIB_BUILD
// JSONC 注释剔除：保留字符串字面量内容，移除 // 与 /* */ 注释
godot::String strip_jsonc_comments(const godot::String &p_jsonc) {
	godot::String result;
	bool in_string = false;
	bool escape_next = false;

	for (int i = 0; i < p_jsonc.length(); i++) {
		const char32_t c = p_jsonc[i];

		if (escape_next) {
			result += c;
			escape_next = false;
			continue;
		}

		if (in_string && c == '\\') {
			escape_next = true;
			result += c;
			continue;
		}

		if (c == '"') {
			in_string = !in_string;
			result += c;
			continue;
		}

		if (!in_string && c == '/' && i + 1 < p_jsonc.length()) {
			const char32_t next = p_jsonc[i + 1];
			if (next == '/') { // 行注释
				while (i < p_jsonc.length() && p_jsonc[i] != '\n') {
					i++;
				}
				continue; // 保留换行符，由外层循环落入 result
			}
			if (next == '*') { // 块注释
				i += 2;
				while (i + 1 < p_jsonc.length() && !(p_jsonc[i] == '*' && p_jsonc[i + 1] == '/')) {
					i++;
				}
				i++; // 跳过 '/'（若 i+1 越界则无害）
				continue;
			}
		}

		result += c;
	}

	return result;
}
#	endif // JSB_EDITOR_LIB_BUILD

} // namespace

#	if JSB_RUNTIME_LIB_BUILD
bool PathsMapping::refresh() {
	return refresh_from(get_paths_mapping_path());
}

bool PathsMapping::refresh_from(const godot::String &p_file_path) {
	s_mappings.clear();

	godot::Ref<godot::FileAccess> file = godot::FileAccess::open(p_file_path, godot::FileAccess::READ);
	if (!file.is_valid()) {
		JSB_LOG(Verbose, "PathsMapping: no .paths_mapping found");
		return false; // 尚未生成（如编辑器首次启动），留待 Editor 通知刷新
	}

	int line_no = 0;
	godot::String line;
	while (!file->eof_reached()) {
		line = file->get_line().strip_edges();
		line_no++;
		if (line.is_empty() || line.begins_with("#")) {
			continue; // 空行与注释
		}
		const int64_t eq = line.find("=");
		if (eq < 0) {
			JSB_LOG(Warning, "PathsMapping: skipped malformed line %d: '%s'", line_no, line);
			continue;
		}
		const godot::String pattern = line.substr(0, eq);
		const godot::String candidates_str = line.substr(eq + 1);
		if (candidates_str.is_empty()) {
			continue; // 无候选的 pattern 无意义
		}

		// 加载时拆解 pattern（含一个 '*'：前缀 + 后缀；无 '*'：精确匹配）。
		// split 的 allow_empty 必须为 true：'*' 在末尾时后缀是空段，false 会把
		// 空段丢掉导致 has_star 误判（"abc/*" 被当成无星 "abc/"，捕获错误丢弃）
		PathsEntry entry;
		const PackedStringArray pattern_splits = pattern.split("*", true, 1);
		entry.has_star = pattern_splits.size() > 1;
		entry.pattern_prefix = pattern_splits[0];
		entry.pattern_suffix = entry.has_star ? pattern_splits[1] : "";

		// 加载时拆解候选（'.' = 恒等重写；含一个 '*'：捕获替换；无 '*'：捕获丢弃整串）
		for (const godot::String &candidate_str : candidates_str.split(",")) {
			if (candidate_str.is_empty()) {
				continue;
			}

			// 同上：allow_empty 必须为 true（末尾星的后缀是空段）
			PathsCandidate candidate;
			const PackedStringArray candidate_splits = candidate_str.split("*", true, 1);
			candidate.has_star = candidate_splits.size() > 1;
			candidate.prefix = candidate_splits[0];
			candidate.suffix = candidate.has_star ? candidate_splits[1] : "";

			entry.candidates.push_back(candidate);
		}
		if (entry.candidates.is_empty()) {
			continue;
		}
		s_mappings.push_back(entry);
	}

	s_mappings.sort_custom<PathsEntryComparator>();
	JSB_LOG(Verbose, "PathsMapping: loaded %d entries", s_mappings.size());
	return true;
}

bool PathsMapping::resolve(const godot::String &p_import_path, void *p_userdata, CandidateTryFn p_try_resolve) {
	if (s_mappings.is_empty()) {
		return false;
	}
	for (const PathsEntry &entry : s_mappings) { // pattern 前缀长度降序
		if (entry.has_star) {
			// 通配符匹配：begins_with(前缀) && ends_with(后缀)，捕获为中间部分
			//（'*' 在末尾时后缀为空，ends_with("") 恒真）
			if (!p_import_path.begins_with(entry.pattern_prefix) || !p_import_path.ends_with(entry.pattern_suffix)) {
				continue;
			}
		} else {
			// 精确匹配（pattern 无 '*'）
			if (p_import_path != entry.pattern_prefix) {
				continue;
			}
		}

		const godot::String captured = p_import_path.substr(
				entry.pattern_prefix.length(),
				p_import_path.length() - entry.pattern_prefix.length() - entry.pattern_suffix.length());

		// 候选按 tsconfig 声明顺序逐个替换并尝试（拼一个试一个）
		for (const PathsCandidate &candidate : entry.candidates) {
			// TS 官方 sub.replace("*", captured) 语义：
			// 候选有 '*' → 前缀 + 捕获 + 后缀；候选无 '*' → 捕获丢弃，结果 = 候选整串
			const godot::String replaced = candidate.has_star
					? candidate.prefix + captured + candidate.suffix
					: candidate.prefix;
			if (p_try_resolve(p_userdata, replaced)) {
				return true;
			}
		}
		return false; // TS 语义：只取最长匹配 pattern 的候选，不回落其他 pattern
	}
	return false;
}
#	endif // JSB_RUNTIME_LIB_BUILD

#	if JSB_EDITOR_LIB_BUILD
bool PathsMapping::generate_from_tsconfig(const godot::String &p_jsonc_content) {
	godot::Ref<godot::JSON> json_parser = memnew(godot::JSON);
	const godot::Error err = json_parser->parse(strip_jsonc_comments(p_jsonc_content));
	if (err != godot::OK) {
		JSB_LOG(Error, "PathsMapping: failed to parse tsconfig.json: error %d", err);
		return false;
	}

	// 没有路径映射配置：删除旧映射文件，返回成功（用户未使用 paths 功能）
	const godot::Variant data_var = json_parser->get_data();
	if (data_var.get_type() != godot::Variant::DICTIONARY) {
		JSB_LOG(Warning, "PathsMapping: tsconfig.json root is not an object");
		return false;
	}
	const godot::Dictionary data = data_var;

	const godot::Variant compiler_options_var = data.get("compilerOptions", godot::Variant());
	if (compiler_options_var.get_type() != godot::Variant::DICTIONARY) {
		JSB_LOG(Verbose, "PathsMapping: no compilerOptions in tsconfig.json");
		return internal::PathUtil::delete_file(get_paths_mapping_path());
	}
	const godot::Dictionary compiler_options = compiler_options_var;

	if (!compiler_options.has("paths")) {
		JSB_LOG(Verbose, "PathsMapping: no paths in tsconfig.json");
		return internal::PathUtil::delete_file(get_paths_mapping_path());
	}
	const godot::Variant paths_var = compiler_options["paths"];
	if (paths_var.get_type() != godot::Variant::DICTIONARY) {
		JSB_LOG(Warning, "PathsMapping: 'paths' is not an object");
		return false;
	}

	// TS paths 官方语义（moduleResolution reference / moduleResolver.ts）：
	// - pattern 至多一个 '*'：匹配 = begins_with(前缀) && ends_with(后缀)，捕获为中间部分；
	//   无 '*' 为全等匹配（"jquery" 不会匹配 "jqueryui"）
	// - 候选替换 = 候选首个 '*' 由捕获替换（sub.replace 语义）；候选无 '*' 时捕获丢弃
	// - 候选按声明顺序逐个尝试直到解析成功（fallbacks）
	// - 基准：相对 baseUrl（若设置）或 tsconfig 所在目录（TS 4.1+），我们即 res://
	// - 候选不会嵌套再匹配 paths（值是查找位置而非模块说明符），无需展开
	godot::Vector<godot::String> lines;
	const godot::Dictionary paths = paths_var;
	for (const godot::Variant &key_var : paths.keys()) {
		if (key_var.get_type() != godot::Variant::STRING) {
			JSB_LOG(Warning, "PathsMapping: skipped non-string paths key");
			continue;
		}
		const godot::String pattern = key_var;

		const godot::Variant value_var = paths[key_var];
		if (value_var.get_type() != godot::Variant::ARRAY) {
			JSB_LOG(Warning, "PathsMapping: paths['%s'] is not an array, skipped", pattern);
			continue;
		}

		godot::Vector<godot::String> candidates;
		const godot::Array replacements = value_var;
		for (const godot::Variant &replacement_var : replacements) {
			if (replacement_var.get_type() != godot::Variant::STRING) {
				JSB_LOG(Warning, "PathsMapping: paths['%s'] contains a non-string candidate, skipped", pattern);
				continue;
			}
			godot::String candidate = replacement_var;
			if (candidate.is_empty()) {
				continue;
			}
			// 候选规范化：折叠 "./"、"../" 中间段与重复 '/'（基准是 res://，即 tsconfig 所在目录）。
			// simplify_path 会剥掉尾随 '/'：有 '*' 候选的 '*' 前分隔符不受影响；
			// 无 '*' 候选捕获整个丢弃（sub.replace 语义），尾 '/' 无拼接意义。
			candidate = candidate.simplify_path();
			if (candidate == ".") {
				// "./" 整体映射到基准目录：等价于恒等重写，保留原文 "."
				candidates.push_back(".");
				continue;
			}
			// 候选含格式分隔符（'='、','）会破坏行格式，refresh 无法正确拆解，拒绝
			if (candidate.contains("=") || candidate.contains(",")) {
				JSB_LOG(Warning, "PathsMapping: paths['%s'] candidate '%s' contains a reserved separator, skipped", pattern, candidate);
				continue;
			}
			// 越出项目根的 "../"（res:// 是虚拟文件系统根，无法向上）与协议头
			if (candidate.begins_with("/") || candidate.begins_with("../") || candidate.contains("://")
					|| (candidate.length() >= 2 && candidate[1] == ':')) {
				JSB_LOG(Warning, "PathsMapping: paths['%s'] candidate '%s' escapes the res:// root, skipped", pattern, candidate);
				continue;
			}
			candidates.push_back(candidate);
		}

		// 保存原文格式 "pattern=候选1,候选2,..."（'=' 与 ',' 为格式分隔符，原文含之会被
		// refresh 跳过并告警，等效于非法配置）。pattern 不做规范化：'*' 位置是匹配语义的一部分。
		if (!candidates.is_empty() && !pattern.contains("=") && !pattern.contains(",")) {
			godot::String line = pattern + godot::String("=");
			for (int i = 0; i < candidates.size(); i++) {
				if (i > 0) {
					line += godot::String(",");
				}
				line += candidates[i];
			}
			lines.push_back(line);
		}
	}

	// 写入顺序无关紧要：refresh() 加载后统一 sort_custom 按 pattern 前缀长度降序
	godot::Ref<godot::FileAccess> file = godot::FileAccess::open(get_paths_mapping_path(), godot::FileAccess::WRITE);
	if (!file.is_valid()) {
		JSB_LOG(Warning, "PathsMapping: cannot open %s for writing: %s", get_paths_mapping_path(), godot::UtilityFunctions::error_string(godot::FileAccess::get_open_error()));
		return false;
	}
	for (const godot::String &line : lines) {
		file->store_line(line);
	}

	JSB_LOG(Verbose, "PathsMapping: generated %d mappings", lines.size());
	return true;
}

godot::String PathsMapping::get_paths_mapping_file_path() {
	return get_paths_mapping_path();
}
#	endif // JSB_EDITOR_LIB_BUILD

} // namespace jsb

#endif // JSB_USE_TYPESCRIPT
