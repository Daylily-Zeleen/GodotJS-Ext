/************************************************************************/
/*  jsb_script_language.h                                               */
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
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of      */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU    */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once

#include <compat/jsb_compat.h>
#include <runtime/bridge/jsb_bridge.h>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/classes/script_language_extension.hpp>
#include <godot_cpp/classes/thread.hpp>
#include <godot_cpp/templates/self_list.hpp>

class GodotJSScript;

namespace jsb {
struct JSEnvironment {
private:
	bool is_shadow_;
	std::shared_ptr<jsb::Environment> target_;

	void init();

public:
	// p_path_hint is only used for logging
	JSEnvironment(const String &p_path_hint, bool p_is_shadow_allowed);

	~JSEnvironment();

	JSEnvironment(const JSEnvironment &) = delete;
	JSEnvironment &operator=(const JSEnvironment &) = delete;

	JSEnvironment(JSEnvironment &&p_other) noexcept {
		is_shadow_ = p_other.is_shadow_;
		target_ = std::move(p_other.target_);
	}
	JSEnvironment &operator=(JSEnvironment &&p_other) noexcept {
		if (this != &p_other) {
			is_shadow_ = p_other.is_shadow_;
			target_ = std::move(p_other.target_);
		}
		return *this;
	}

	jsb_no_discard bool is_shadow() const { return is_shadow_; }

	jsb::Environment *operator->() {
		init();
		return target_.get();
	}

	operator std::shared_ptr<jsb::Environment>() {
		init();
		return target_;
	}
};
} //namespace jsb

using ScriptInstancePropertyState = List<Pair<StringName, Variant>>;

class GodotJSScriptLanguage : public ScriptLanguageExtension {
	GDCLASS(GodotJSScriptLanguage, ScriptLanguageExtension)

private:
	friend class GodotJSScript;
	friend class GodotJSScriptInstance;
	friend class GodotJSScriptInstanceBase;
	friend class ResourceFormatLoaderGodotJSScript;
	friend struct jsb::JSEnvironment;

	struct ShadowEnvironment {
		ThreadEx::ID thread_id = ThreadEx::UNASSIGNED_ID;
		std::shared_ptr<jsb::Environment> holder;
		int rc = 0;
	};

#if JSB_DEBUG
	struct ScriptCallProfileInfo {
		uint64_t total_time = 0;
		uint64_t total_calls = 0;
		uint64_t frame_time = 0;
		uint64_t frame_calls = 0;
		uint64_t last_frame_time = 0;
		uint64_t last_frame_calls = 0;
	};

	struct ScriptClassProfileInfo {
		String path;
		HashMap<StringName, ScriptCallProfileInfo> methods;
	};

	struct ScriptCallProfileInfoMap {
		bool enabled = false;
		HashMap<StringName, ScriptClassProfileInfo> classes;
	};
#endif

	static GodotJSScriptLanguage *singleton_;

	mutable std::recursive_mutex mutex_;
	SelfList<GodotJSScript>::List script_list_;

	bool once_initialized_ = false;
	uint64_t last_ticks_ = 0;
	std::shared_ptr<jsb::Environment> environment_;

	mutable std::recursive_mutex shadow_mutex_;
	std::vector<ShadowEnvironment> shadow_environments_;

#if JSB_DEBUG
	ScriptCallProfileInfoMap profile_info_map_;
#endif

	Ref<RegEx> ts_class_name_matcher_;

	// [JS] export & declare in two lines, matches 'class ClassName extends BaseName' + 'exports.default = ClassName'
	Ref<RegEx> js_class_name_matcher2_;

	// [JS] export & declare in a single line, matches 'exports.default = class ClassName extends BaseName'
	Ref<RegEx> js_class_name_matcher1_;

#if JSB_TOOLS
	// 源码里按标识符定位声明：`_find_function`（语言层）与 `GodotJSScript::_get_member_line`
	// （脚本层）共用，两处结论因而一致。
	// 只认「行首 + 可选修饰符 + 标识符 + 声明后继」，声明后继取自 JS/TS 里真实的成员声明形态
	// （`name(`、`name =`、`name:`、`name;`）。这样不会匹配到注释里的同名文字
	// （「// bar mentioned」后面是空格+字母），也不会匹配到长名的前缀（找 `bar` 不命中 `barbaz`）。
	// 代价：泛型方法 `foo<T>()` 匹配不到（引入 `<` 会把 `a < b` 之类的比较式误判成声明）。
	Ref<RegEx> js_declaration_matcher_;
#endif // JSB_TOOLS

public:
	static GodotJSScriptLanguage *get_singleton();

	/** @brief Check if the language has been initialized. */
	_FORCE_INLINE_ bool is_initialized() const { return once_initialized_; }

	/**
	 * @brief Get the main JS environment.
	 * @note Can only be call from the main thread.
	 * @return The JS environment.
	 */
	_FORCE_INLINE_ std::shared_ptr<jsb::Environment> get_environment() const {
		jsb_check(once_initialized_ && environment_ && Thread::is_main_thread());
		return environment_;
	}

	void scan_external_changes();

#if JSB_TOOLS
	/**
	 * 在源码里定位 `p_identifier` 的声明行（1 基），找不到返回 -1。
	 * 供 `GodotJSScript::_get_member_line` 复用，保证与 `_find_function` 用同一套匹配规则
	 * （见 `js_declaration_matcher_`）。
	 * @note 纯源码文本扫描，不加载模块，可在 EditorFileSystem 的后台扫描路径上调用。
	 */
	int find_identifier_line(const String &p_identifier, const String &p_source) const;
#endif // JSB_TOOLS

#if JSB_DEBUG
	void add_script_call_profile_info(const String &p_path, const StringName &p_class, const StringName &p_method, uint64_t p_time);
#endif

	bool is_global_class_generic(const String &p_path) const;

	template <size_t N>
	jsb::JSValueMove eval_source(const char (&p_code)[N], Error &r_err) {
		return environment_->eval_source(p_code, (int)N - 1, "eval", r_err);
	}

	jsb::JSValueMove eval_source(const String &p_code, Error &r_err) {
		const CharString str = p_code.utf8();
		return environment_->eval_source(str.get_data(), str.length(), "eval", r_err);
	}

	/**
	 * @brief Evaluate `p_code` with `p_arg` exposed as the transient global `__jsb_arg`.
	 *
	 * @note Main thread only. Returns an invalid JSValueMove and ERR_UNCONFIGURED
	 *       when the language is not initialized, ERR_UNAVAILABLE off the main
	 *       thread, ERR_INVALID_PARAMETER when the argument cannot cross into JS
	 *       and whatever the evaluated source raised otherwise.
	 */
	jsb::JSValueMove eval_source_with_arg(const String &p_code, const Variant &p_arg, Error &r_err);

	GodotJSScriptLanguage();
	virtual ~GodotJSScriptLanguage() override;

	virtual void _init() override;
	virtual void _finish() override;
	virtual void _frame() override;

#if JSB_TOOLS
	virtual bool _is_control_flow_keyword(const String &p_keyword) const override;

	virtual TypedArray<Dictionary> _get_built_in_templates(const StringName &p_object) const override;

	virtual PackedStringArray _get_doc_comment_delimiters() const override;
	virtual PackedStringArray _get_comment_delimiters() const override;
	virtual PackedStringArray _get_string_delimiters() const override;

	virtual Dictionary _validate(const String &p_script, const String &p_path, bool p_validate_functions, bool p_validate_errors, bool p_validate_warnings, bool p_validate_safe_lines) const override;
	virtual Ref<Script> _make_template(const String &p_template, const String &p_class_name, const String &p_base_class_name) const override;

	virtual bool _supports_documentation() const override { return true; }

	virtual bool _is_using_templates() override { return true; }
	virtual bool _supports_builtin_mode() const override { return false; }

	virtual int32_t _find_function(const String &p_function, const String &p_code) const override;

	// Godot 的函数添加只能在文件末尾，不符合类的定义范围有前后标记的语言，该功能不实现。
	virtual bool _can_make_function() const override { return false; }
	virtual String _make_function(const String &p_class_name, const String &p_function_name, const PackedStringArray &p_function_args) const override { return ""; }

	// Auto indent. This IS reached: the editor's `EditorAdapter::format_code`
	// (script_language_extension.h:310-312) forwards to `auto_indent_code`
	// (:573), which is a `GDVIRTUAL3RC_REQUIRED` -- a real virtual dispatch that
	// lands here. The hook receives the whole text plus the line range the user
	// selected, and must return the whole text with that range re-indented; the
	// caller writes the lines back (script_text_editor.cpp:1804-1808). It can be
	// called once per caret range, so it must be a pure function of the input.
	virtual String _auto_indent_code(const String &p_code, int32_t p_from_line, int32_t p_to_line) const override;

	virtual TypedArray<Dictionary> _get_public_functions() const override { return {}; } // TODO: Vector<StackInfo>
	virtual Dictionary _get_public_constants() const override { return Dictionary(); } // TODO: Vector<StackInfo>
	virtual TypedArray<Dictionary> _get_public_annotations() const override { return {}; } // TODO: Vector<StackInfo>

	virtual bool _handles_global_class_type(const String &p_type) const override;
	virtual Dictionary _get_global_class_name(const String &p_path) const override;

	// 用户自行设置外部文本编辑器即可。
	virtual Error _open_in_external_editor(const Ref<Script> &p_script, int32_t p_line, int32_t p_column) override { return OK; }
	virtual bool _overrides_external_editor() override { return false; }

	//
	virtual bool _can_inherit_from_file() const override { return false; } // js 类不能直接继承文件路径
	virtual String _validate_path(const String &p_path) const override;

	// 暂无计划实现编辑器内编写 TS/JS 脚本
	virtual Dictionary _complete_code(const String &p_code, const String &p_path, Object *p_owner) const override { return {}; }
	virtual Dictionary _lookup_code(const String &p_code, const String &p_symbol, const String &p_path, Object *p_owner) const override { return {}; }
#endif // JSB_TOOLS

	virtual void _thread_enter() override;
	virtual void _thread_exit() override;

	virtual String _get_name() const override;
	virtual String _get_type() const override;

#if JSB_USE_TYPESCRIPT
	virtual String _get_extension() const override { return JSB_TYPESCRIPT_EXT; }
#else
	virtual String _get_extension() const override { return JSB_JAVASCRIPT_EXT; }
#endif

#if JSB_DEBUG
	virtual void _reload_all_scripts() override;
	virtual void _reload_scripts(const Array &p_scripts, bool p_soft_reload) override;
#endif
#if JSB_TOOLS
	virtual void _reload_tool_script(const Ref<Script> &p_script, bool p_soft_reload) override;
#endif

	virtual PackedStringArray _get_recognized_extensions() const override;

	// Autoload 钩子：引擎通过它们把 autoload 对象交给脚本语言。
	// - `_add_global_constant`：运行期（非编辑器），只针对单例（`*` 前缀）的 autoload；调用两次——
	//   先传 `Variant()` 占位，再传实例化后的节点。
	// - `_add_named_global_constant`：编辑器 Autoload 面板装载/改动，以及一次以 `Variant()` 预注册名字。
	// - `_remove_named_global_constant`：编辑器里 autoload 被移除或改名。
	// 引擎分成两套是因为运行期的表是索引数组（索引进字节码，不可删），编辑器的是 name->value 映射（可删）。
	// TODO: 实现 autoload 暴露。待定：(1) 环境隔离——`Environment` 按 isolate 划分，钩子不带环境参数，
	//       值要定义到当时存活的每个环境、之后新建的也要补；(2) 编辑器 `.d.ts` 里 autoload 的类型来源
	//       （类型来自 autoload 挂的脚本，不是钩子传的值）。
	virtual void _add_global_constant(const StringName &p_name, const Variant &p_value) override {}
	virtual void _add_named_global_constant(const StringName &p_name, const Variant &p_value) override {}
	virtual void _remove_named_global_constant(const StringName &p_name) override {}

	virtual PackedStringArray _get_reserved_words() const override;

#if JSB_DEBUG
	/** NOTE: 调试功能未经实测 */
	virtual String _debug_get_error() const override;
	virtual int32_t _debug_get_stack_level_count() const override;
	virtual int32_t _debug_get_stack_level_line(int32_t p_level) const override;
	virtual String _debug_get_stack_level_function(int32_t p_level) const override;
	virtual String _debug_get_stack_level_source(int32_t p_level) const override;

	// 以下三个 hook 在本 VM 的嵌入 API 上**无法作答**，因此如实返回空，而不是编造条目。
	// 这是查证过的，不是推测：
	//
	//   - v8 的作用域内省（`ScopeIterator`、`v8::Debug` 命名空间）在本仓 vendor 的
	//     头文件里**根本不存在**：穷举 `third/v8/include/*.h`，与调试相关的只有
	//     `StackTrace` / `StackFrame` / `Message`（`v8-debug.h`）。局部变量/作用域
	//     只能经 Chrome DevTools 协议，从 `jsb_debugger.cpp` 里那条 websocket 会话
	//     异步取得 —— 那是另一套调试器，不是一次函数调用能拿到的。
	//   - 因此被暂停的 JS 帧的局部变量在此不可知；帧所属的实例同样不可知：JSB 不记录
	//     「当前正在执行的 script instance」，而纯 JS 帧也从不经过那个记录。
	//
	// 返回空恰好是引擎需要的行为（不是随便返回）：
	//   - `debug_get_stack_level_instance` 返回 nullptr，才会让 `get_stack_frame_vars`
	//     跳过 `self` 条目（core/debugger/remote_debugger.cpp:507），
	//     并让 `evaluate` 直接退出（同文件 :554-556），而不是拿一个错的实例去求值。
	virtual Dictionary _debug_get_stack_level_locals(int32_t p_level, int32_t p_max_subitems, int32_t p_max_depth) override { return Dictionary(); }
	virtual Dictionary _debug_get_stack_level_members(int32_t p_level, int32_t p_max_subitems, int32_t p_max_depth) override { return Dictionary(); }
	virtual void *_debug_get_stack_level_instance(int32_t p_level) override { return nullptr; }

	virtual Dictionary _debug_get_globals(int32_t p_max_subitems, int32_t p_max_depth) override;
	virtual String _debug_parse_stack_level_expression(int32_t p_level, const String &p_expression, int32_t p_max_subitems, int32_t p_max_depth) override;
	virtual TypedArray<Dictionary> _debug_get_current_stack_info() override;

	virtual void _profiling_start() override;
	virtual void _profiling_stop() override;
	virtual void _profiling_set_save_native_calls(bool p_enable) override;

	virtual int32_t _profiling_get_accumulated_data(ScriptLanguageExtensionProfilingInfo *p_info_array, int32_t p_info_max) override;
	virtual int32_t _profiling_get_frame_data(ScriptLanguageExtensionProfilingInfo *p_info_array, int32_t p_info_max) override;
#endif // JSB_DEBUG

private:
#if JSB_DEBUG
	// 供那些 const 的 `_debug_get_stack_level_*` hook 读取的快照。放在这里是为了让
	// 引擎的一次枚举（先 count、再逐层取）只走一遍 VM，而不是每层走一遍；
	// `count` 与 `get_error` 会重取，取层级的只在「还没有快照」时重取。
	struct DebugStackSnapshot {
		jsb::DebugStackFrameList frames;
		// 待处理异常的文本（若有）。这是运行时唯一能说明「错误来自哪里」的地方。
		String error;
		bool valid = false;
	};
	mutable DebugStackSnapshot debug_stack_;

	// 从 VM 重取 `debug_stack_`，并把每一帧映射回可编辑的源文件（见实现处）。
	void _refill_debug_stack() const;

	// 在主 realm 里求值 `p_expression`，并把结果整理成调试面板要的形式。
	// 返回值表示是否求到了值；失败时 `r_error`（若给出）拿到 VM 自己的错误文本。
	bool _evaluate_debug_expression(const String &p_expression, Variant &r_value, String *r_error = nullptr) const;
#endif // JSB_DEBUG

	std::shared_ptr<jsb::Environment> create_shadow_environment();
	void destroy_shadow_environment(const std::shared_ptr<jsb::Environment> &p_env);

#if JSB_DEBUG
	void reload_scripts_internal(const Array &p_scripts, bool p_soft_reload);
#endif

	static void populate_string_names_replacements();

protected:
	static void _bind_methods();
};
