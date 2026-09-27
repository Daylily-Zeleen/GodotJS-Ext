/************************************************************************/
/*  test_jsb_editor_cleanup.h                                           */
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

// `collect_invalid_files` 决定菜单项 "Cleanup Invalid Files"（`cleanup_invalid_files`）会删掉
// jsb 输出目录里的哪些文件。
//
// 签名清单（`.sig`）与编译产物同目录、扩展名不在 `.js`/`.cjs`/`.mjs` 之列 ⇒ 若没有专门分支，
// 每个 sidecar 都会被既有规则当成陈旧产物删掉（**有**对应 `.ts` 源也照删）⇒ 清理菜单会静默
// 清空全部签名清单。两个方向都必须断言：有源的保留、无源的删除。
//
// 夹具的目录布局必须与真实布局一致：产物在 `<outDir>/<rel>.{js,sig}`，而"源是否存在"的查询
// 落点是 `res://<rel>.ts`（**项目根**下的相对路径，见 `PathUtil::convert_*_path`），
// 不是 outDir 内。把两者放同一个目录会让"应保留"的断言恒假。

#include "../tests/jsb_test_utils.h"
#include "../weaver-editor/jsb_editor_plugin.h"
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>

namespace jsb::editor::tests {

namespace cleanup_fixture {

constexpr const char *kFixtureName = "test_cleanup_fixture";

// 两个目录一起管：产物目录（outDir 之下）与源目录（项目根之下）。
struct Fixture {
	String artifact_dir; // <outDir>/test_cleanup_fixture
	String source_dir; // res://test_cleanup_fixture
	Vector<String> created;

	Fixture() {
		artifact_dir = internal::settings::get_jsb_out_res_path().path_join(kFixtureName);
		source_dir = String("res://").path_join(kFixtureName);
	}

	~Fixture() {
		// `DirAccess::remove_absolute` 只删文件或空目录 ⇒ 先删文件再删目录。
		for (const String &path : created) {
			DirAccess::remove_absolute(path);
		}
		CHECK(DirAccess::remove_absolute(artifact_dir) == Error::OK);
		CHECK(DirAccess::remove_absolute(source_dir) == Error::OK);
	}

	// 需要的是「磁盘上存在这个路径」，内容与扩展名语义无关。
	Error touch(const String &p_path) {
		const Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
		if (file.is_null()) return FileAccess::get_open_error();
		file->store_string("fixture\n");
		created.push_back(p_path);
		return OK;
	}
};

} //namespace cleanup_fixture

TEST_CASE("[editor] [cleanup] invalid file collection keeps sidecars whose source still exists") {
	using namespace cleanup_fixture;
	const String out_root = internal::settings::get_jsb_out_res_path();

	// `collect_invalid_files` 对打不开的目录直接返回空集 ⇒ 若输出目录不存在，下面的
	// "应被收集"断言会全部空转通过。
	REQUIRE(DirAccess::make_dir_recursive_absolute(out_root) == OK);

	Fixture fx;
	REQUIRE(DirAccess::make_dir_recursive_absolute(fx.artifact_dir) == OK);
	REQUIRE(DirAccess::make_dir_recursive_absolute(fx.source_dir) == OK);

	// 产物：一对有源、一个源已消失
	REQUIRE(fx.touch(fx.artifact_dir.path_join("paired.sig")) == OK);
	REQUIRE(fx.touch(fx.source_dir.path_join("paired.ts")) == OK);
	REQUIRE(fx.touch(fx.artifact_dir.path_join("orphaned.sig")) == OK);
	// 既有规则的两侧对照（`.js` 的源也是 `.ts`）
	REQUIRE(fx.touch(fx.artifact_dir.path_join("live.js")) == OK);
	REQUIRE(fx.touch(fx.source_dir.path_join("live.ts")) == OK);
	REQUIRE(fx.touch(fx.artifact_dir.path_join("dead.js")) == OK);

	Vector<String> invalid;
	GodotJSEditorPlugin::collect_invalid_files(fx.artifact_dir, invalid);

	// 新分支的两个方向
	CHECK_MESSAGE(!invalid.has(fx.artifact_dir.path_join("paired.sig")),
			"a sidecar whose .ts source exists must be kept (otherwise Cleanup Invalid Files wipes every signature)");
	CHECK_MESSAGE(invalid.has(fx.artifact_dir.path_join("orphaned.sig")),
			"a sidecar whose .ts source is gone must still be collected");
	// 回归守卫：新分支不得吞掉既有规则
	CHECK_MESSAGE(!invalid.has(fx.artifact_dir.path_join("live.js")), "a .js with its .ts source must be kept");
	CHECK_MESSAGE(invalid.has(fx.artifact_dir.path_join("dead.js")), "a .js without its .ts source must be collected");
}

} //namespace jsb::editor::tests
