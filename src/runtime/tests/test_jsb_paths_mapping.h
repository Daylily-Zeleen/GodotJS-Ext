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

#pragma once

#if JSB_USE_TYPESCRIPT

#	include "jsb_test_helpers.h"

#	include <internal/jsb_paths_mapping.h>

#	include <godot_cpp/classes/dir_access.hpp>
#	include <godot_cpp/classes/file_access.hpp>

namespace jsb::tests {

namespace {
// 测试回调：记录 resolve 产出的全部候选 ID（供断言），全部返回 false（模拟解析失败）
struct ResolveRecorder {
	Vector<String> candidates;
};

bool record_candidate(void *p_userdata, const String &p_candidate_id) {
	((ResolveRecorder *)p_userdata)->candidates.push_back(p_candidate_id);
	return false;
}

// 在临时目录生成假 .paths_mapping 并加载；析构时从项目标准路径恢复真实映射
//（s_mappings 是全局状态，测试必须复位，否则污染后续运行时行为）
struct MappingFixture {
	bool loaded = false;

	MappingFixture(const Vector<String> &p_lines) {
		static const String kFixturePath = String("res://.godot/godotjs_ext/tests_fixture/.paths_mapping");

		Ref<DirAccess> da = DirAccess::open("res://");
		if (da.is_valid() && !da->dir_exists(".godot/godotjs_ext/tests_fixture")) {
			da->make_dir_recursive(".godot/godotjs_ext/tests_fixture");
		}
		Ref<FileAccess> f = FileAccess::open(kFixturePath, FileAccess::WRITE);
		if (!f.is_valid()) {
			return;
		}
		for (const String &line : p_lines) {
			f->store_line(line);
		}
		f.unref(); // flush before reload

		loaded = PathsMapping::refresh_from(kFixturePath);
	}

	~MappingFixture() {
		// 恢复项目真实映射（文件不存在时同样清空全局表，正确）
		PathsMapping::refresh();
	}
};
} // namespace

TEST_CASE("[runtime] [jsb.paths_mapping] k=v line parsing: legal and malformed lines") {
	Vector<String> lines;
	lines.push_back("@tests/*=tests/*");
	lines.push_back("jquery=vendor/jquery");
	lines.push_back("# comment line");
	lines.push_back(""); // empty line
	lines.push_back("malformed line without eq");
	lines.push_back("@empty=");
	lines.push_back("*=libs/*");
	MappingFixture fx(lines);
	REQUIRE(fx.loaded);

	// 合法条目加载（注释/空行跳过，坏行告警跳过，空候选 pattern 丢弃）
	ResolveRecorder rec;
	PathsMapping::resolve("@tests/paths_test/paths-test", &rec, record_candidate);
	REQUIRE(rec.candidates.size() == 1);
	CHECK(rec.candidates[0] == "tests/paths_test/paths-test");
}

TEST_CASE("[runtime] [jsb.paths_mapping] wildcard: prefix+suffix match and star capture") {
	MappingFixture fx({ "vendor/*/index=lib/*/main" });
	REQUIRE(fx.loaded);

	// 中间通配符：前后缀都参与匹配，捕获为中间部分
	{
		ResolveRecorder rec;
		PathsMapping::resolve("vendor/mylib/index", &rec, record_candidate);
		REQUIRE(rec.candidates.size() == 1);
		CHECK(rec.candidates[0] == "lib/mylib/main");
	}
	// 后缀不匹配：不命中，无候选产出
	{
		ResolveRecorder rec;
		PathsMapping::resolve("vendor/mylib/other", &rec, record_candidate);
		CHECK(rec.candidates.is_empty());
	}
	// 前缀不匹配
	{
		ResolveRecorder rec;
		PathsMapping::resolve("other/mylib/index", &rec, record_candidate);
		CHECK(rec.candidates.is_empty());
	}
}

TEST_CASE("[runtime] [jsb.paths_mapping] no-star pattern is exact match, not prefix") {
	MappingFixture fx({ "jquery=vendor/jquery/dist/jquery" });
	REQUIRE(fx.loaded);

	// 全等命中
	{
		ResolveRecorder rec;
		PathsMapping::resolve("jquery", &rec, record_candidate);
		REQUIRE(rec.candidates.size() == 1);
		CHECK(rec.candidates[0] == "vendor/jquery/dist/jquery");
	}
	// 前缀相似不命中（精确匹配不会匹配 jqueryui）
	{
		ResolveRecorder rec;
		PathsMapping::resolve("jqueryui", &rec, record_candidate);
		CHECK(rec.candidates.is_empty());
	}
}

TEST_CASE("[runtime] [jsb.paths_mapping] candidate without star drops the capture") {
	MappingFixture fx({ "@drop/*=flat" });
	REQUIRE(fx.loaded);

	ResolveRecorder rec;
	PathsMapping::resolve("@drop/anything/here", &rec, record_candidate);
	REQUIRE(rec.candidates.size() == 1);
}

TEST_CASE("[runtime] [jsb.paths_mapping] catch-all pattern (bare '*')") {
	MappingFixture fx({ "*=libs/*" });
	REQUIRE(fx.loaded);

	ResolveRecorder rec;
	PathsMapping::resolve("some/module", &rec, record_candidate);
	REQUIRE(rec.candidates.size() == 1);
	CHECK(rec.candidates[0] == "libs/some/module");
}

TEST_CASE("[runtime] [jsb.paths_mapping] longest pattern prefix wins, no fallback to other patterns") {
	MappingFixture fx({ "@t=candidate_short", "@tests/*=candidate_long/*" });
	REQUIRE(fx.loaded);

	ResolveRecorder rec;
	PathsMapping::resolve("@tests/a/b", &rec, record_candidate);
	REQUIRE(rec.candidates.size() == 1);
	CHECK(rec.candidates[0] == "candidate_long/a/b");
}

TEST_CASE("[runtime] [jsb.paths_mapping] fallback: candidates tried in declared order") {
	MappingFixture fx({ "@tests/*=first/*,second/*" });
	REQUIRE(fx.loaded);

	ResolveRecorder rec;
	PathsMapping::resolve("@tests/paths_test/paths-test", &rec, record_candidate);
	REQUIRE(rec.candidates.size() == 2);
	CHECK(rec.candidates[0] == "first/paths_test/paths-test");
	CHECK(rec.candidates[1] == "second/paths_test/paths-test");
}

TEST_CASE("[runtime] [jsb.paths_mapping] resolve stops at first accepted candidate") {
	MappingFixture fx({ "@x/*=ok/*,never/*" });
	REQUIRE(fx.loaded);

	struct Ctx {
		ResolveRecorder rec;
	} ctx;
	bool accepted = PathsMapping::resolve("@x/m", &ctx, [](void *p_userdata, const String &p_candidate_id) -> bool {
		if (p_candidate_id.begins_with("ok/")) {
			return true; // 模拟解析成功
		}
		((Ctx *)p_userdata)->rec.candidates.push_back(p_candidate_id);
		return false;
	});
	CHECK(accepted);
	REQUIRE(ctx.rec.candidates.is_empty()); // 首个候选即成功，无后续
}

TEST_CASE("[runtime] [jsb.paths_mapping] missing file clears mappings and returns false") {
	MappingFixture fx({ "@keep/*=kept/*" }); // 失败的 refresh_from 会清空表，fixture 析构负责恢复
	REQUIRE(fx.loaded);

	CHECK(!PathsMapping::refresh_from("res://.godot/godotjs_ext/tests_fixture/.does_not_exist"));

	// 映射表为空：resolve 直接返回 false，零候选
	ResolveRecorder rec;
	CHECK(!PathsMapping::resolve("@keep/x", &rec, record_candidate));
	CHECK(rec.candidates.is_empty());
}

TEST_CASE("[runtime] [jsb.paths_mapping] resolve is pattern-agnostic about specifier shape") {
	// resolve 本身不区分裸/相对/绝对说明符——门控在 DefaultModuleResolver（调用方）。
	// 这里验证 resolve 对相对形态的 pattern 同样正常匹配。
	MappingFixture fx({ "./rel/*=mapped/*" });
	REQUIRE(fx.loaded);

	ResolveRecorder rec;
	PathsMapping::resolve("./rel/file", &rec, record_candidate);
	REQUIRE(rec.candidates.size() == 1);
	CHECK(rec.candidates[0] == "mapped/file");
}

TEST_CASE("[runtime] [jsb.paths_mapping] trailing-star candidate keeps wildcard semantics") {
	// 回归守卫：'*' 在末尾的候选（"abc/*"）必须保持捕获拼接语义，
	// 不得因 split 丢空段而误判为无星候选（捕获丢弃）。
	MappingFixture fx({ "@x/*=abc/*" });
	REQUIRE(fx.loaded);

	ResolveRecorder rec;
	PathsMapping::resolve("@x/abcdef", &rec, record_candidate);
	REQUIRE(rec.candidates.size() == 1);
	CHECK(rec.candidates[0] == "abc/abcdef");
}

TEST_CASE("[runtime] [jsb.paths_mapping] candidate parsing is independent of the pattern") {
	// 回归守卫：候选拆分不得复用 pattern（曾因复制粘贴拆错变量，
	// 候选前后缀取自 pattern，重写结果完全错误）。
	MappingFixture fx({ "@pat/*=cand-prefix/*/cand-suffix" });
	REQUIRE(fx.loaded);

	ResolveRecorder rec;
	PathsMapping::resolve("@pat/middle", &rec, record_candidate);
	REQUIRE(rec.candidates.size() == 1);
	CHECK(rec.candidates[0] == "cand-prefix/middle/cand-suffix");
}

} // namespace jsb::tests

#endif // JSB_USE_TYPESCRIPT
