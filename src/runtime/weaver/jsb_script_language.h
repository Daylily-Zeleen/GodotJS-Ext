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

	// 不适用：`ScriptLanguageExtension::auto_indent_code` 是**静态成员**，`EditorAdapter::format_code`
	// 直接值调用它，不经过虚分派 ⇒ 本覆写在 extension 路径下永远不会被调用（「自动缩进」走
	// `EditorLanguage::format_code`）。要支持自动缩进需实现 `EditorLanguage` 侧，而不是这里。
	virtual String _auto_indent_code(const String &p_code, int32_t p_from_line, int32_t p_to_line) const override { return p_code; }

	// 以下两个不适用：唯一调用方是 autoload（`editor/settings/editor_autoload_settings.cpp` 的
	// 增/删/占名，与 `main/main.cpp:4490/4538` 游戏启动的两遍）。语义是「让脚本里的**裸标识符**
	// 解析到该值」——TS/JS 没有裸标识符解析（strict 下未声明即 ReferenceError），本仓也没有
	// 任何 autoload 集成；上游 C# 对 `add_named` / `remove_named` 二者不覆写（基类空实现）。
	// 第三个同类钩子 `_add_global_constant` 因不属于 `#if JSB_TOOLS` 区段，声明在下方
	// `_get_recognized_extensions` 之后，那里有对应说明。
	virtual void _add_named_global_constant(const StringName &p_name, const Variant &p_value) override {}
	virtual void _remove_named_global_constant(const StringName &p_name) override {}

	// 以下三个不适用：唯一消费者是 `editor/doc/doc_tools.cpp`，为语言生成 `@<语言名>` 文档页。
	// 要做需要新增并维护一份 C++ 侧的语言内建清单；本仓的 `@bind` 注解在运行时 bundle 里，
	// 不满足该同步接口。上游 C# 同样为空实现。
	virtual TypedArray<Dictionary> _get_public_functions() const override { return {}; }
	virtual Dictionary _get_public_constants() const override { return Dictionary(); }
	virtual TypedArray<Dictionary> _get_public_annotations() const override { return {}; }

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

	// 不适用：唯一调用方是 autoload（`main.cpp:4490/4538` 启动两遍）。GDScript 借此把核心常量
	// 灌进 `globals` 数组**供解析器做常量折叠**，JS 没有这个阶段；本仓亦无 autoload 集成。
	// 上游 C# 对同名钩子为空实现。
	virtual void _add_global_constant(const StringName &p_name, const Variant &p_value) override {}

	virtual PackedStringArray _get_reserved_words() const override;

#if JSB_DEBUG
	virtual String _debug_get_error() const override { return ""; } // TODO
	virtual int32_t _debug_get_stack_level_count() const override { return 1; } // TODO
	virtual int32_t _debug_get_stack_level_line(int32_t p_level) const override { return 1; } // TODO
	virtual String _debug_get_stack_level_function(int32_t p_level) const override { return ""; } // TODO
	virtual String _debug_get_stack_level_source(int32_t p_level) const override { return ""; } // TODO
	virtual Dictionary _debug_get_stack_level_locals(int32_t p_level, int32_t p_max_subitems, int32_t p_max_depth) override { return Dictionary(); } // TODO
	virtual Dictionary _debug_get_stack_level_members(int32_t p_level, int32_t p_max_subitems, int32_t p_max_depth) override { return Dictionary(); } // TODO
	virtual void *_debug_get_stack_level_instance(int32_t p_level) override { return nullptr; } // TODO
	virtual Dictionary _debug_get_globals(int32_t p_max_subitems, int32_t p_max_depth) override { return Dictionary(); } // TODO
	virtual String _debug_parse_stack_level_expression(int32_t p_level, const String &p_expression, int32_t p_max_subitems, int32_t p_max_depth) override { return ""; } // TODO
	virtual TypedArray<Dictionary> _debug_get_current_stack_info() override { return {}; } // TODO: Vector<StackInfo>

	virtual void _profiling_start() override;
	virtual void _profiling_stop() override;
	virtual void _profiling_set_save_native_calls(bool p_enable) override;

	virtual int32_t _profiling_get_accumulated_data(ScriptLanguageExtensionProfilingInfo *p_info_array, int32_t p_info_max) override;
	virtual int32_t _profiling_get_frame_data(ScriptLanguageExtensionProfilingInfo *p_info_array, int32_t p_info_max) override;
#endif // JSB_DEBUG

private:
	std::shared_ptr<jsb::Environment> create_shadow_environment();
	void destroy_shadow_environment(const std::shared_ptr<jsb::Environment> &p_env);

#if JSB_DEBUG
	void reload_scripts_internal(const Array &p_scripts, bool p_soft_reload);
#endif

	static void populate_string_names_replacements();

protected:
	static void _bind_methods();
};
