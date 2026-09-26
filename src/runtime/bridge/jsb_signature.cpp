/************************************************************************/
/*  jsb_signature.cpp                                                   */
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

#include "jsb_signature.h"

#include "jsb_environment.h"
#include "jsb_string_names.h"

#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/method_bind.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace jsb::internal {

namespace {

constexpr char kMagic[] = "JSIG";
/** 清单版本。与提取器的 `VERSION` 必须一致，不符即整份拒绝（旧版清单按"没有清单"处理）。 */
constexpr uint8_t kVersion = 1;

/** 签名记录里的 kind。 */
enum MemberKind : uint8_t {
	MemberKind_Method = 0,
	MemberKind_Signal = 1,
};

/** 逐签名的 flags 位。 */
enum : uint8_t {
	SigFlag_HasReturn = 1 << 0,
	SigFlag_IsVararg = 1 << 1,
};

/** 逐参数的 flags 位。 */
enum : uint8_t {
	ParamFlag_Optional = 1 << 0,
};

/** 只读游标。任何越界/格式错误都让 `ok()` 变假，由调用方整体放弃这份清单。 */
class Cursor {
	const uint8_t *const _data;
	const int64_t _size;
	int64_t _offset = 0;
	bool _ok = true;

public:
	Cursor(const uint8_t *p_data, int64_t p_size) : _data(p_data), _size(p_size) {}

	_FORCE_INLINE_ bool ok() const { return _ok; }

	uint8_t u8() {
		if (!_ok || _offset + 1 > _size) {
			_ok = false;
			return 0;
		}
		return _data[_offset++];
	}

	bool raw(uint8_t *p_dst, int64_t p_length) {
		if (!_ok || p_length < 0 || _offset + p_length > _size) {
			_ok = false;
			return false;
		}
		memcpy(p_dst, _data + _offset, (size_t)p_length);
		_offset += p_length;
		return true;
	}

	/** LEB128（与提取器的 `varuint` 对应）。 */
	uint64_t varuint() {
		uint64_t result = 0;
		int shift = 0;
		while (true) {
			const uint8_t byte = u8();
			if (!_ok) return 0;
			result |= (uint64_t)(byte & 0x7f) << shift;
			if ((byte & 0x80) == 0) return result;
			shift += 7;
			if (shift >= 64) {
				// 畸形长度：继续移位是 UB，直接判失败。
				_ok = false;
				return 0;
			}
		}
	}
};

struct PoolReader {
	Vector<String> strings;

	/** 索引 0 恒为空串（"无标注"哨兵），越界一律返回空串而不是崩。 */
	const String &get(uint64_t p_index) const {
		static const String empty;
		return p_index < (uint64_t)strings.size() ? strings[(int64_t)p_index] : empty;
	}
};

/** 清单里单个成员/字符串的规模上限：纯粹用于拒绝畸形的长度字段，避免无界预留。 */
constexpr uint64_t kMaxElements = 0x100000;

// `Variant::get_type_name` 的反向表。**现构而非硬编码**：引擎新增 Variant 类型时自动跟上。
const HashMap<StringName, Variant::Type> &_get_variant_type_names() {
	static const HashMap<StringName, Variant::Type> table = []() {
		HashMap<StringName, Variant::Type> map;
		for (int i = 0; i < (int)Variant::VARIANT_MAX; ++i) {
			const Variant::Type type = (Variant::Type)i;
			// NIL 的名字是 "Nil"：排除它，免得把"未知类型"当成某个内建类型。
			if (type == Variant::NIL) continue;
			const String type_name = Variant::get_type_name(type);
			if (!type_name.is_empty()) {
				map.insert(type_name, type);
			}
		}
		return map;
	}();
	return table;
}

// 引擎别名集（`scripts/typings/godot.generated.d.ts` + `type.extension.d.ts`）。它们在 AST 上
// **保留原文**，在 type checker 上会退化成 `number`/`string` ⇒ 只能按原文识别。
const HashMap<String, Variant::Type> &_get_engine_aliases() {
	static const HashMap<String, Variant::Type> aliases = []() {
		HashMap<String, Variant::Type> map;
		map.insert("byte", Variant::INT);
		map.insert("int32", Variant::INT);
		map.insert("uint32", Variant::INT);
		map.insert("int64", Variant::INT);
		map.insert("uint64", Variant::INT);
		map.insert("float32", Variant::FLOAT);
		map.insert("float64", Variant::FLOAT);
		map.insert("StringName", Variant::STRING_NAME);
		return map;
	}();
	return aliases;
}

// TS 侧未用引擎别名的 JS 原语写法。
const HashMap<String, Variant::Type> &_get_primitive_aliases() {
	static const HashMap<String, Variant::Type> aliases = []() {
		HashMap<String, Variant::Type> map;
		map.insert("number", Variant::FLOAT);
		map.insert("string", Variant::STRING);
		map.insert("boolean", Variant::BOOL);
		return map;
	}();
	return aliases;
}

/** 参数位：NIL 表示"任意值"（镜像 `MethodBind::get_argument_info` 对无类型参数的编码）。 */
_FORCE_INLINE_ PropertyInfo _make_argument_info(Variant::Type p_type, const StringName &p_name, const StringName &p_class_name = StringName()) {
	const uint32_t usage = p_type == Variant::NIL
			? (PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT)
			: PROPERTY_USAGE_DEFAULT;
	return PropertyInfo(p_type, p_name, PROPERTY_HINT_NONE, String(), usage, p_class_name);
}

/** 返回值位：NIL **带** `NIL_IS_VARIANT` = "不可映射，按任意值处理"；**不带** = 真 `void`。 */
_FORCE_INLINE_ PropertyInfo _make_unmapped_return_info(const StringName &p_name) {
	return PropertyInfo(Variant::NIL, p_name, PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT);
}

} //namespace

bool signature_load(const StringName &p_module_id,
		HashMap<StringName, LocalVector<ScriptMethodSignature>> &r_methods,
		HashMap<StringName, LocalVector<PropertyInfo>> &r_signals) {
	r_methods.clear();
	r_signals.clear();

	const String module_path = String(p_module_id);
	if (!module_path.ends_with("." JSB_JAVASCRIPT_EXT)) {
		return false;
	}
	// 与编译产物同名同目录，只换扩展名（与编辑器侧 `collect_invalid_files` 的推导式一致）。
	//NOTE `std::size` 计入结尾 NUL，所以减 `std::size(...) - 1` 只剥掉扩展名字母、**保留那个点**；
	//     减 `std::size(...)` 会把点一起吃掉（`x.js` -> `xsig`），是同一个 off-by-one 陷阱。
	const String stem = module_path.substr(0, module_path.length() - (std::size(JSB_JAVASCRIPT_EXT) - 1));
	const String sig_path = stem + String(JSB_SIGNATURE_EXT);
	if (!FileAccess::file_exists(sig_path)) {
		return false;
	}
	const PackedByteArray bytes = FileAccess::get_file_as_bytes(sig_path);
	if (FileAccess::get_open_error() != OK || bytes.is_empty()) {
		return false;
	}

	Cursor cursor(bytes.ptr(), bytes.size());
	char magic[sizeof(kMagic) - 1] = {};
	if (!cursor.raw((uint8_t *)magic, sizeof(magic)) || memcmp(magic, kMagic, sizeof(magic)) != 0) {
		return false;
	}
	if (cursor.u8() != kVersion) {
		// 版本不符：按"没有清单"处理，退回函数源文本扫描。不做迁移 —— 清单是构建产物。
		JSB_LOG(Verbose, "obsolete signature manifest (version mismatch), ignored: %s", sig_path);
		return false;
	}
	uint8_t source_md5[16] = {};
	if (!cursor.raw(source_md5, sizeof(source_md5))) {
		return false;
	}
	jsb_unused(source_md5); // 增量判断在提取器侧完成，运行时不比对

	PoolReader pool;
	{
		const uint64_t pool_count = cursor.varuint();
		if (!cursor.ok() || pool_count > kMaxElements) return false;
		// 池里**每一项**都带 `[len][bytes]`，索引 0 也不例外（长度 0）。必须逐项消费长度前缀，
		// 否则从索引 1 起步会漏掉一个字节、整条流从此错位。索引 0 恒为空串 = "无标注"哨兵。
		PackedByteArray buffer; // 复用同一块缓冲，避免每个字符串各分配一次
		for (uint64_t i = 0; i < pool_count; ++i) {
			const uint64_t length = cursor.varuint();
			if (!cursor.ok() || length > kMaxElements) return false;
			if (length == 0) {
				pool.strings.push_back(String());
				continue;
			}
			buffer.resize((int64_t)length + 1);
			if (buffer.size() != (int64_t)length + 1) return false;
			uint8_t *const dst = buffer.ptrw();
			if (!cursor.raw(dst, (int64_t)length)) return false;
			dst[length] = 0;
			pool.strings.push_back(String::utf8((const char *)dst, (int)length));
		}
	}

	const uint64_t member_count = cursor.varuint();
	if (!cursor.ok() || member_count > kMaxElements) return false;
	for (uint64_t m = 0; m < member_count; ++m) {
		const uint8_t kind = cursor.u8();
		const uint64_t name_index = cursor.varuint();
		const uint64_t signature_count = cursor.varuint();
		if (!cursor.ok() || signature_count > kMaxElements) return false;
		const StringName member_name(pool.get(name_index));
		if (member_name.is_empty()) {
			return false;
		}

		if (kind == MemberKind_Signal) {
			// 信号：恒单签名、返回值恒 void ⇒ 只取参数表。
			for (uint64_t s = 0; s < signature_count; ++s) {
				const uint8_t sig_flags = cursor.u8();
				if (sig_flags & SigFlag_HasReturn) {
					cursor.varuint();
				}
				const uint64_t param_count = cursor.varuint();
				cursor.varuint(); // default_count：信号不设默认值
				if (!cursor.ok() || param_count > kMaxElements) return false;
				LocalVector<PropertyInfo> &arguments = r_signals[member_name];
				arguments.clear();
				for (uint64_t p = 0; p < param_count; ++p) {
					cursor.u8(); // param flags：可选性对信号无意义
					const uint64_t param_name = cursor.varuint();
					const uint64_t param_type = cursor.varuint();
					if (!cursor.ok()) return false;
					arguments.push_back(signature_resolve_type(pool.get(param_type), pool.get(param_name), true));
				}
			}
			continue;
		}

		if (kind != MemberKind_Method) {
			return false; // 不认识的记录类型：整份拒绝，别猜
		}
		LocalVector<ScriptMethodSignature> &overloads = r_methods[member_name];
		overloads.clear();
		for (uint64_t s = 0; s < signature_count; ++s) {
			const uint8_t sig_flags = cursor.u8();
			ScriptMethodSignature signature;
			signature.is_vararg = (sig_flags & SigFlag_IsVararg) != 0;
			if (sig_flags & SigFlag_HasReturn) {
				const uint64_t return_index = cursor.varuint();
				if (!cursor.ok()) return false;
				const String return_name = pool.get(return_index);
				// 返回值位：真 void ⇒ NIL **不带** NIL_IS_VARIANT；不可映射 ⇒ NIL **带**（design §6.2）。
				signature.return_val = return_name.is_empty()
						? _make_unmapped_return_info(StringName())
						: signature_resolve_type(return_name, StringName(), false);
			}
			const uint64_t param_count = cursor.varuint();
			const uint64_t default_count = cursor.varuint();
			if (!cursor.ok() || param_count > kMaxElements) return false;
			signature.default_count = (int)default_count;
			for (uint64_t p = 0; p < param_count; ++p) {
				cursor.u8(); // param flags：可选性已由 default_count 表达
				const uint64_t param_name = cursor.varuint();
				const uint64_t param_type = cursor.varuint();
				if (!cursor.ok()) return false;
				signature.arguments.push_back(signature_resolve_type(pool.get(param_type), pool.get(param_name), true));
			}
			overloads.push_back(std::move(signature));
		}
	}

	return cursor.ok();
}

PropertyInfo signature_resolve_type(const String &p_type_name, const StringName &p_name, bool p_is_argument) {
	// `void` 只在返回值位有意义 —— 真 void（取它的返回值会报错）。参数位上的 `void` 按"任意值"处理。
	if (p_type_name == "void") {
		return p_is_argument ? _make_argument_info(Variant::NIL, p_name)
							 : PropertyInfo(Variant::NIL, p_name, PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_DEFAULT);
	}

	if (p_type_name.is_empty()) {
		// 未标注：参数位是"任意值"；返回值位是"不可映射/未知"，同样按任意值处理。
		return p_is_argument ? _make_argument_info(Variant::NIL, p_name) : _make_unmapped_return_info(p_name);
	}

	// 1. 引擎别名。
	if (const Variant::Type *hit = _get_engine_aliases().getptr(p_type_name)) {
		return _make_argument_info(*hit, p_name);
	}

	// 2. TS 侧的原语名。
	if (const Variant::Type *hit = _get_primitive_aliases().getptr(p_type_name)) {
		return _make_argument_info(*hit, p_name);
	}

	// 3. 反查**原始类名**再查表。JS 侧暴露名不等于 Godot 原始名（`GArray` -> `Array`、
	//    `GDictionary` -> `Dictionary`，camel_case 下还有更多），不先反查一律命中不了。
	const StringName original_name = StringNames::get_singleton().get_original_name(StringName(p_type_name));

	if (const Variant::Type *hit = _get_variant_type_names().getptr(original_name)) {
		return _make_argument_info(*hit, p_name);
	}

	// 4. Godot 类（仍用原始名：inspector 按原始类名显示）。
	if (ClassDB::class_exists(original_name)) {
		return PropertyInfo(Variant::OBJECT, p_name, PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_DEFAULT, original_name);
	}

	// 5. 不可映射（接口、联合、函数类型、泛型实参……）。
	return p_is_argument ? _make_argument_info(Variant::NIL, p_name) : _make_unmapped_return_info(p_name);
}

MethodInfo signature_to_method_info(const StringName &p_name, const ScriptMethodSignature &p_signature) {
	MethodInfo info(p_name); // ctor 已置 `GDEXTENSION_METHOD_FLAG_NORMAL`
	if (p_signature.is_vararg) {
		// 剩余参数：不进 `arguments`，只置 VARARG —— GDScript 只在**非** VARARG 时检查实参上限
		// （`gdscript_analyzer.cpp:6146`），不加会对 `obj.add(1, 2, 3)` 报 Too many arguments。
		info.flags |= GDEXTENSION_METHOD_FLAG_VARARG;
	}
	// 返回值原样搬运：真 void 与"不可映射"的区分由 `return_val.usage` 的
	// `PROPERTY_USAGE_NIL_IS_VARIANT` 位承载（design.md §6.2）。
	info.return_val = p_signature.return_val;

	const int64_t argument_count = p_signature.arguments.size();
	info.arguments.resize((int)argument_count);
	for (int64_t i = 0; i < argument_count; ++i) {
		info.arguments[(int)i] = p_signature.arguments[i];
	}
	// 可选参数走 `default_arguments`：值取不到（TS 默认值是表达式，无法求值），只放占位 Variant。
	// **个数**才是 arity 的依据 —— GDScript 的最小参数数 = `arguments.size() - default_arguments.size()`
	// （`gdscript_analyzer.cpp:6143`），这也是"填 args 是安全的"的前提（design.md §6.3）。
	const int default_count = p_signature.default_count > 0 && p_signature.default_count <= (int)argument_count
			? p_signature.default_count
			: 0;
	for (int i = 0; i < default_count; ++i) {
		info.default_arguments.push_back(Variant());
	}
	return info;
}

int resolve_declared_parameter_count(Environment *p_env, const StringName &p_module_id, const StringName &p_exposed_name) {
	if (p_env == nullptr || !p_env->is_caller_thread()) {
		// 跨线程进入 isolate 不安全（与环境既有的 `check_internal_state` 同一条约束）。
		return ScriptArgumentCount::Unknown;
	}
	JavaScriptModule *module = p_env->get_module_cache().find(p_module_id);
	if (module == nullptr) {
		return ScriptArgumentCount::Unknown;
	}
	ScriptClassInfoPtr class_info = p_env->find_script_class(module->script_class_id);
	if (!class_info) {
		return ScriptArgumentCount::Unknown;
	}

	v8::Isolate *isolate = p_env->get_isolate();
	JSB_ISOLATE_SCOPE(isolate);
	v8::HandleScope handle_scope(isolate);
	const v8::Local<v8::Context> context = p_env->get_context();
	v8::Context::Scope context_scope(context);

	// 复用脚本类信息里的 `method_cache`，与 `Environment::call_script_method` 同机制
	// （`jsb_environment.cpp:1931-1945`）：命中即用，未命中则从 prototype 取一次并缓存。
	v8::Local<v8::Function> method_func;
	const TypeGen<StringName, v8::Global<v8::Function>>::UnorderedMapIt it = class_info->method_cache.find(p_exposed_name);
	if (it == class_info->method_cache.end()) {
		const v8::Local<v8::Object> class_obj = class_info->js_class.Get(isolate);
		v8::Local<v8::Value> prototype_val;
		if (!class_obj->Get(context, jsb_name(p_env, prototype)).ToLocal(&prototype_val)
				|| !prototype_val->IsObject()) {
			return ScriptArgumentCount::Unknown;
		}
		v8::Local<v8::Value> method_val;
		if (prototype_val.As<v8::Object>()->Get(context, p_env->get_string_value(p_exposed_name)).ToLocal(&method_val)
				&& method_val->IsFunction()) {
			method_func = method_val.As<v8::Function>();
			class_info->method_cache[p_exposed_name] = v8::Global<v8::Function>(isolate, method_func);
		} else {
			class_info->method_cache[p_exposed_name] = v8::Global<v8::Function>();
		}
	} else if (!it->second.IsEmpty()) {
		method_func = it->second.Get(isolate);
	}

	if (method_func.IsEmpty()) {
		return ScriptArgumentCount::Unknown;
	}
	return count_declared_parameters(impl::Helper::to_string_without_side_effect(isolate, method_func));
}

int count_declared_parameters(const String &p_source) {
	const int64_t length = p_source.length();
	const int64_t open = p_source.find("(");
	if (open < 0) {
		return ScriptArgumentCount::Unknown;
	}

	int depth = 0;
	int count = 0;
	bool has_token = false; // 当前顶层分段是否已有内容
	bool is_rest = false; // …且该分段以剩余参数运算符开头
	bool terminated = false;

	for (int64_t index = open + 1; index < length; ++index) {
		const char32_t ch = p_source[index];

		if (ch == '\'' || ch == '"' || ch == '`') {
			// 跳过字面量（尊重反斜杠转义）。模板字面量整体跳过 ⇒ 其中 `${}` 里的 `,` 根本不会遇到。
			// 嵌套反引号（`f(a = `x${`y`}z`)`）会提前结束跳过并使扫描失同步 —— 与下面的正则字面量
			// 同属 best effort。
			for (++index; index < length; ++index) {
				if (p_source[index] == '\\') {
					++index;
				} else if (p_source[index] == ch) {
					break;
				}
			}
			has_token = true;
			continue;
		}

		if (ch == '/') {
			if (index + 1 < length && p_source[index + 1] == '/') {
				const int64_t newline = p_source.find("\n", index);
				index = newline < 0 ? length : newline;
			} else if (index + 1 < length && p_source[index + 1] == '*') {
				const int64_t close = p_source.find("*/", index + 2);
				index = close < 0 ? length : close + 1;
			}
			//NOTE 正则字面量里的不平衡括号会让深度计数失同步（区分正则与除法需要完整文法）——
			//     这类输入最终多半以"未闭合"收场并返回 Unknown，属具名盲点。
			continue;
		}

		if (ch == '(' || ch == '[' || ch == '{') {
			++depth;
			has_token = true;
			continue;
		}

		if (ch == ')' || ch == ']' || ch == '}') {
			if (depth > 0) {
				--depth;
				has_token = true;
				continue;
			}
			// depth-0 上的非 `)` 闭合符说明文本已失同步，而只有 `)` 能合法收束形参表。
			if (ch != ')') {
				return ScriptArgumentCount::Unknown;
			}
			terminated = true;
			break;
		}

		if (ch == ',') {
			if (depth > 0) {
				has_token = true;
			} else {
				if (has_token && !is_rest) {
					++count;
				}
				has_token = false;
				is_rest = false;
			}
			continue;
		}

		if (depth == 0 && !has_token && ch == '.' && index + 2 < length
				&& p_source[index + 1] == '.' && p_source[index + 2] == '.') {
			is_rest = true;
			has_token = true;
			index += 2;
			continue;
		}

		// `char_utils.hpp` 没有 `is_whitespace`，空格集合手写。
		if (depth == 0 && ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r' && ch != '\f' && ch != '\v') {
			has_token = true;
		}
	}

	if (!terminated) {
		// 形参表没闭合：V8 会把超长源文本截断（前 111 字符 + 省略标记），此时无法界定。**不得报 0**。
		return ScriptArgumentCount::Unknown;
	}
	return count + (has_token && !is_rest ? 1 : 0);
}

} //namespace jsb::internal
