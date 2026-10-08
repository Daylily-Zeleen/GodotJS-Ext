/************************************************************************/
/*  jsb_class_info.h                                                    */
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

#include "internal/jsb_bit_field.h"
#include "jsb_bridge_pch.h"
#include "jsb_module.h"

namespace jsb {
enum class FinalizationType : uint8_t {
	// break the binding, no delete
	None,

	//
	Default,
};

typedef void (*ConstructorFunc)(const v8::FunctionCallbackInfo<v8::Value> &);
typedef void (*FinalizerFunc)(Environment *, void *, FinalizationType);

/**
 * @brief Correspond with [NativeClassType::Type::Custom].
 * Typically,get `CustomNativeBase*` from js object's internal field use `dynamic_cast<>()` to cast to child class.
 */
class CustomNativeBase {
};

namespace NativeClassType {
//NOTE the enum value of Type must be a even number, since it's stored as AlignedPointerInternalField
enum Type : uint8_t {
	// never used
	None = 0,

	//TODO a FASTPATH implementation? avoid unnecessary Variant wrapping for special builtin primitives (from Vector2 to Color)
	//     But a VALUE still can not be binded as VALUE itself, it seems impossible to avoid thread-safe TYPED pools. Is it worth to implement?
	// [BEGIN] RESERVED FOR FUTURE USE
	// Vector2 = 2,
	// Vector3 = 10,
	// Color = 32,
	// [END  ] RESERVED FOR FUTURE USE
	_RESERVED = 32,

	// Godot Variant classes (valuetype).
	// Classes are anonymously registered in Environment, only support retrieving NativeClassInfo by ClassID.
	// Variant is a special case, it's fully managed by JS without an object mapping in object_db.
	GodotPrimitive = 34,

	// Godot Object classes are registered with name in Environment,
	// support retrieving ClassID by the class name from godot_classes_index_.
	// unnecessary but used to avoid class lookup.
	GodotObject = 36,

	// type for JSWorker.
	// unnecessary but used to avoid class lookup.
	Worker = 38,

	// type for Shadow.
	// unnecessary but used to avoid class lookup.
	Shadow = 40,

	// reserved for future use
	Custom = 64,
};
} //namespace NativeClassType

struct NativeBindingInfo {
	void *ptr;
	NativeClassType::Type type;
};

struct NativeClassInfo {
	// the func to release the exposed C++ (godot/variant/native) object
	// it's called when a JS value with this class type garbage collected by JS runtime
	FinalizerFunc finalizer;

	NativeClassType::Type type;

	// *only if type == GodotObject*
	// godot_object_constructor use this name to look up classdb
	StringName name;

	impl::Class clazz;
};

// Safe pointer of NativeClassInfo
typedef internal::SArray<NativeClassInfo, NativeClassID>::Pointer NativeClassInfoPtr;
typedef internal::SArray<NativeClassInfo, NativeClassID>::ConstPointer NativeClassInfoConstPtr;

struct ClassRegister;
typedef NativeClassInfoPtr (*ClassRegisterFunc)(const ClassRegister &p_register, NativeClassID *r_class_id);

namespace ScriptClassDocField {
enum Type {
	Deprecated = 0,
	Experimental = 1,
	Help = 2,
};
}

#if JSB_TOOLS
struct ScriptBaseDoc {
	/// Brief one-liner (the source comment's first line). Also the historical `@bind.help()` slot.
	String brief_description;

	/// Full text (the whole source comment). Mirrors `DocData::*Doc::description`.
	String description;

	String deprecated_message;
	String experimental_message;

	bool is_deprecated = false;
	bool is_experimental = false;
};

struct ScriptClassDoc : ScriptBaseDoc {};
struct ScriptMethodDoc : ScriptBaseDoc {};
struct ScriptPropertyDoc : ScriptBaseDoc {};
struct ScriptSignalDoc : ScriptBaseDoc {};
struct ScriptConstantDoc : ScriptBaseDoc {};
#else
struct ScriptClassDoc {};
struct ScriptMethodDoc {};
struct ScriptPropertyDoc {};
struct ScriptSignalDoc {};
struct ScriptConstantDoc {};
#endif

namespace ScriptMethodFlags {
enum Type : uint8_t {
	None = 0,
	Static = 1,
};
}

namespace ScriptArgumentCount {
enum : int {
	// 尚未解析（懒加载：解析期不做任何签名工作，首次查询时才去读清单/函数源文本）
	NotComputed = -2,

	// 已尝试解析但无法判定（形参表闭合不了：正则字面量里的不平衡括号、模板字面量里嵌套的反引号、
	// V8 把源文本截断到前 111 字符）。**不得当成 0** —— 那会把"读不出来"谎报成"没有参数"。
	Unknown = -1,
};
}

struct ScriptSignalInfo {
	// 信号参数表（清单 `kind=1` 的记录）。**无清单时为空**，与改动前"只存名字"的行为一致。
	// 信号恒为单签名、返回值恒 void ⇒ 不复用 ScriptMethodSignature。
	LocalVector<PropertyInfo> arguments;

#if JSB_TOOLS
	ScriptSignalDoc doc;
#endif
};

// 一个重载签名（清单里 `kind=method` 的一条记录）。无重载时 `overloads` 只含一项。
struct ScriptMethodSignature {
	// `void` 与"不可映射"的区分见 design.md §6.2：
	// type == NIL 且**不带** usage & PROPERTY_USAGE_NIL_IS_VARIANT ⇒ 真 void；
	// 带该 flag ⇒ 不可映射，按"任意值"处理。
	PropertyInfo return_val;

	// 剩余参数**不在**表内 —— 它由 is_vararg 表达（对齐 GDScriptFunction::_argument_count）。
	LocalVector<PropertyInfo> arguments;

	// 可选参数个数。消费端据此填 MethodInfo::default_arguments（占位 Variant），
	// GDScript 的最小 arity = arguments.size() - default_count（design.md §6.1）。
	// 默认值本身取不到（TS 表达式无法求值）⇒ 只记"有默认值"这一事实。
	int default_count = 0;

	bool is_vararg = false;
};

namespace ScriptConstantKind {
enum Type : uint8_t {
	// a plain JS primitive value (NIL / BOOL / INT / FLOAT / STRING / STRING_NAME)
	Value,

	// a normalized `Dictionary{name: int}` built by the parser from a JS numeric enum object
	Enum,

	// a container (ARRAY / DICTIONARY) which shares `_p` with the JS side,
	// therefore read-only is applied recursively on both sides
	Container,
};
}

struct ScriptConstantInfo {
	StringName name;

	// already converted; a numeric enum is normalized into `Dictionary{name: int}`
	Variant value;

	ScriptConstantKind::Type kind = ScriptConstantKind::Value;

#if JSB_TOOLS
	ScriptConstantDoc doc;
#endif
};

struct ScriptStaticVariableInfo {
	StringName name;
	PropertyInfo details;
};

struct ScriptMethodInfo
{
#if JSB_TOOLS
	ScriptMethodDoc doc;
#endif

	ScriptMethodFlags::Type flags = ScriptMethodFlags::None;

	// 已声明参数个数，剩余参数不计。**三态取值**（见 `ScriptArgumentCount`）：
	//   NotComputed —— 尚未解析。解析期不做任何签名工作（懒加载），首次查询时才去读签名清单
	//                  （sidecar），清单缺失时退回函数源文本扫描。
	//   Unknown     —— 已尝试解析但无法判定（形参表闭合不了：正则字面量里的不平衡括号、
	//                  模板字面量里嵌套的反引号、V8 把源文本截断到前 111 字符）。**不得当成 0**
	//                  —— 那会把"读不出来"谎报成"没有参数"。
	//   >= 0        —— 真实个数。
	//
	// 为什么不用 `Function.length`：它是下界而非个数 —— 遇到第一个带默认值的参数或剩余参数就停，
	// `(a, b = 2, ...rest)` 报 1 而不是 3；且各 JS 腿对 `v8::Function` 是否暴露 `Length()` 并不一致。
	// 由 `GodotJSScript::_get_script_method_argument_count` 与
	// `GodotJSScriptInstanceBase::get_method_argument_count` 消费。
	int argument_count = ScriptArgumentCount::NotComputed;

	// 重载集：无重载时为空；有重载时按源码声明顺序存全部签名（对齐 C# 的处理，design.md §11）。
	// **运行期无法从 JS 侧观测到重载**（emit 只留实现体，原型上同名只有一个函数）⇒ 这一信息只能
	// 来自签名清单。清单缺失时保持为空 ⇒ 所有消费点退回"只有名字与参数个数"的既有行为。
	LocalVector<ScriptMethodSignature> overloads;

	// v8::Global<v8::Function> cache_;

	_FORCE_INLINE_ bool is_static() const { return flags & ScriptMethodFlags::Static; }
};

struct ScriptPropertyInfo {
	PropertyInfo details;

#if JSB_TOOLS
	ScriptPropertyDoc doc;
#endif

	// valid only if _Evaluated flag is set in ScriptClassInfo.flags
	Variant default_value;

	bool cache;

	ScriptPropertyInfo() = default;
	ScriptPropertyInfo(Variant::Type p_type, const StringName &p_name, PropertyHint p_hint = PROPERTY_HINT_NONE, const String &p_hint_string = "", uint32_t p_usage = PROPERTY_USAGE_DEFAULT, const StringName &p_class_name = "") : details(p_type, p_name, p_hint, p_hint_string, p_usage, p_class_name) {};
};

namespace ScriptClassFlags {
enum Type : uint8_t {
	None = 0,

	//TODO we have no idea about it with javascript itself. maybe we can decorate the abstract class and check here?
	Abstract = 1 << 0,
	Tool = 1 << 1,

	// (INTERNAL USE ONLY) whether the default value of properties are evaluated or not
	_Evaluated = 1 << 2,
};
}

// exchange internal javascript class (object) information.
struct StatelessScriptClassInfo {
public:
	// name of the owner module
	StringName module_id;

	// js class name (name of the exported default class in module)
	StringName js_class_name;

	// a fastpath to read the name of native class (the GodotJS class inherits from).
	//NOTE it's a redundant field only for performance. evaluated from 'native_class_id' and must be a godot object class.
	StringName native_class_name;

	// [EXPERIMENTAL] module fastpath for getting script class of base
	StringName base_script_module_id;

	// script icon path for showing in scene hierarchy
	String icon;

#if JSB_TOOLS
	ScriptClassDoc doc;
#endif

	Dictionary rpc_config;

	HashMap<StringName, ScriptMethodInfo> methods;
	HashMap<StringName, ScriptSignalInfo> signals;
	HashMap<StringName, ScriptPropertyInfo> properties;

	// only members annotated with the constant annotation (@bind.exposed.const())
	HashMap<StringName, ScriptConstantInfo> constants;

	// only members annotated with the shared-static annotation (@bind.exposed.shared())
	HashMap<StringName, ScriptStaticVariableInfo> static_variables;

	::templates::BitField<ScriptClassFlags::Type> flags{ ScriptClassFlags::None };

	_FORCE_INLINE_ bool is_tool() const { return flags.has_flag(ScriptClassFlags::Tool); }
	_FORCE_INLINE_ bool is_abstract() const { return flags.has_flag(ScriptClassFlags::Abstract); }
};

struct ScriptClassInfo : StatelessScriptClassInfo {
	// the native class id the current class inherits from.
	NativeClassID native_class_id;

	// SIDE NOTE:
	//     js_class.prototype: prototype definition
	//     js_class.prototype.__proto__: prototype of the base js_class (B.prototype.__proto__ === A.prototype, if B directly extends A)
	//     js_class.constructor: the real function for constructing

	// for constructor access
	v8::Global<v8::Object> js_class;

	internal::TypeGen<StringName, v8::Global<v8::Function>>::UnorderedMap method_cache;

	static void instantiate(Environment *p_env, const StringName &p_module_id, const v8::Local<v8::Object> &p_self);

	static bool _parse_script_class(const v8::Local<v8::Context> &p_context, JavaScriptModule &p_module);
};

// Safe pointer of ScriptClassInfo
typedef internal::SArray<ScriptClassInfo, ScriptClassID> ScriptClassInfoArray;
typedef internal::SArray<ScriptClassInfo, ScriptClassID>::Pointer ScriptClassInfoPtr;
typedef internal::SArray<ScriptClassInfo, ScriptClassID>::ConstPointer ScriptClassInfoConstPtr;

namespace internal {
// Collect the members of a JS class object into `p_class_info`.
// Exposed for the runtime test suite; production callers go through `ScriptClassInfo::_parse_script_class`.
bool _parse_script_class_iterate(const v8::Local<v8::Context> &p_context, const ScriptClassInfoPtr &p_class_info, const v8::Local<v8::Object> &class_obj);
} //namespace internal

} //namespace jsb
