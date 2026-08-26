/************************************************************************/
/*  jsb_test_helpers.h                                                  */
/************************************************************************/
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
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of       */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU   */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once

// Runtime-suite test fixtures (engine-dependent). Engine-independent shared
// utilities live in src/tests/jsb_test_utils.h.

#include "../weaver/jsb_script_language.h"
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>

#include "../tests/jsb_test_utils.h" // doctest config macros + shared utils

namespace jsb::tests {
struct StubBindings {
	static void constructor(const v8::FunctionCallbackInfo<v8::Value> &info) {
	}

	static void method(const v8::FunctionCallbackInfo<v8::Value> &info) {
	}

	static void function(const v8::FunctionCallbackInfo<v8::Value> &info) {
	}
};

struct Utils {
	static void print_exception(const impl::TryCatch &try_catch) {
		if (try_catch.has_caught()) {
			String message;
			try_catch.get_message(&message);
			MESSAGE("JS Exception: ", message);
		}
	}
};

struct CurrentWorkingDirectory {
private:
	String original_working_dir;

	CurrentWorkingDirectory() {
		Ref<DirAccess> da = DirAccess::open("."); // DirAccess::ACCESS_FILESYSTEM
		CHECK(da.is_valid());
		original_working_dir = da->get_current_dir();
		CHECK(!original_working_dir.is_empty());
	}

public:
	static void reset() {
		static CurrentWorkingDirectory env;
		// CHECK(OS::get_singleton()->set_cwd(env.original_working_dir) == OK); // TODO: gde 没办法改变当前的工作目录
	}
};

struct V8ContextScope {
private:
	v8::Isolate *isolate_;
	v8::HandleScope handle_scope_;
	v8::Local<v8::Context> context_;
	v8::Context::Scope context_scope_;

public:
	V8ContextScope(v8::Isolate *isolate)
			: isolate_(isolate)
			, handle_scope_(isolate)
			, context_(isolate->GetCurrentContext())
			, context_scope_(isolate->GetCurrentContext()) {}

	V8ContextScope(jsb::Environment *env)
			: isolate_(env->get_isolate())
			, handle_scope_(env->get_isolate())
			, context_(env->get_context())
			, context_scope_(env->get_context()) {}

	~V8ContextScope() = default;

	operator v8::Isolate *() const { return isolate_; }
};

struct GodotJSScriptLanguageIniter {
public:
	GodotJSScriptLanguageIniter() {
		CHECK(FileAccess::file_exists("project.godot"));
		check_required_files();
		GodotJSScriptLanguage::get_singleton()->_init();
	}

	~GodotJSScriptLanguageIniter() {
		// 清除
		GodotJSScriptLanguage::get_singleton()->_finish();
		// 确保每个测试结束后环境恢复
		GodotJSScriptLanguage::get_singleton()->_init();
	}

private:
	void check_required_files() {
		CHECK(FileAccess::file_exists("./package.json"));
		CHECK(FileAccess::file_exists("./tsconfig.json"));
		CHECK(FileAccess::file_exists("./test_01.ts"));
		CHECK(FileAccess::file_exists("./.godot/godotjs_ext/test_01.js"));
	}
};

} //namespace jsb::tests
