/************************************************************************/
/*  test_jsb_signature.h                                                */
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

#include "../bridge/jsb_signature.h"
#include "../internal/jsb_settings.h"
#include "jsb_test_helpers.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace jsb::tests {

// 签名清单（sidecar）读取器与类型映射。
//
// 清单本身是**编辑器侧构建产物**（`scripts/jsb.editor/src/signature/jsb.signature.extract.cts`），
// 直接依赖它会把这些用例绑到"编辑器跑过一次"这个前置上。这里改为**在测试里自造一份清单字节**：
// 覆盖读取器的全部分支（字符串池 / 多签名 / 可选与剩余参数 / 信号 / 版本与魔数拒绝），
// 以及类型映射的三个关键点（引擎别名、`get_original_name` 反查、void 与不可映射的区分）。
namespace signature_fixture {

/** LEB128，与提取器的 `varuint` 一致。 */
inline void put_varuint(Vector<uint8_t> &r_out, uint64_t p_value) {
	while (p_value >= 0x80) {
		r_out.push_back((uint8_t)((p_value & 0x7f) | 0x80));
		p_value >>= 7;
	}
	r_out.push_back((uint8_t)p_value);
}

/** 逐字段累积的字符串池；索引 0 恒为空串（"无标注"哨兵）。 */
struct Pool {
	Vector<String> strings{ String() };
	HashMap<String, int> index;

	int get(const String &p_value) {
		if (const int *hit = index.getptr(p_value)) {
			return *hit;
		}
		const int id = strings.size();
		strings.push_back(p_value);
		index.insert(p_value, id);
		return id;
	}
};

struct Param {
	String name;
	String type; // 空串 = 无标注
	bool optional = false;
};

struct Signature {
	String return_type; // 空串 = 清单里没有返回类型
	bool is_vararg = false;
	Vector<Param> params;
	int default_count = -1; // < 0 = 按 optional 计数
};

struct Member {
	int kind = 0; // 0=method 1=signal
	String name;
	Vector<Signature> signatures;
};

/** 按 design.md §4 的格式序列化（版本固定为当前版本，便于单独测版本拒绝）。 */
inline Vector<uint8_t> serialize(const Vector<Member> &p_members, uint8_t p_version = 1) {
	Pool pool;
	Vector<uint8_t> out;
	out.push_back('J');
	out.push_back('S');
	out.push_back('I');
	out.push_back('G');
	out.push_back(p_version);
	for (int i = 0; i < 16; ++i) {
		out.push_back(0); // source_md5：运行时不比对
	}

	// 先把池填满（池在头部，必须一次成型），再写记录。
	struct EncodedParam {
		uint8_t flags;
		int name;
		int type;
	};
	struct EncodedSignature {
		uint8_t flags;
		int return_type;
		Vector<EncodedParam> params;
		int default_count;
	};
	struct EncodedMember {
		uint8_t kind;
		int name;
		Vector<EncodedSignature> signatures;
	};
	Vector<EncodedMember> encoded;
	for (const Member &member : p_members) {
		EncodedMember em;
		em.kind = (uint8_t)member.kind;
		em.name = pool.get(member.name);
		for (const Signature &signature : member.signatures) {
			EncodedSignature es;
			es.flags = 0;
			if (!signature.return_type.is_empty()) {
				es.flags |= 1;
			}
			if (signature.is_vararg) {
				es.flags |= 2;
			}
			es.return_type = signature.return_type.is_empty() ? 0 : pool.get(signature.return_type);
			int optionals = 0;
			for (const Param &param : signature.params) {
				EncodedParam ep;
				ep.flags = param.optional ? 1 : 0;
				ep.name = pool.get(param.name);
				ep.type = param.type.is_empty() ? 0 : pool.get(param.type);
				es.params.push_back(ep);
				if (param.optional) {
					++optionals;
				}
			}
			es.default_count = signature.default_count >= 0 ? signature.default_count : optionals;
			em.signatures.push_back(es);
		}
		encoded.push_back(em);
	}

	put_varuint(out, (uint64_t)pool.strings.size());
	for (const String &str : pool.strings) {
		const CharString utf8 = str.utf8();
		put_varuint(out, (uint64_t)utf8.length());
		for (int i = 0; i < utf8.length(); ++i) {
			out.push_back((uint8_t)utf8[i]);
		}
	}

	put_varuint(out, (uint64_t)encoded.size());
	for (const EncodedMember &em : encoded) {
		out.push_back(em.kind);
		put_varuint(out, (uint64_t)em.name);
		put_varuint(out, (uint64_t)em.signatures.size());
		for (const EncodedSignature &es : em.signatures) {
			out.push_back(es.flags);
			if (es.flags & 1) {
				put_varuint(out, (uint64_t)es.return_type);
			}
			put_varuint(out, (uint64_t)es.params.size());
			put_varuint(out, (uint64_t)es.default_count);
			for (const EncodedParam &ep : es.params) {
				out.push_back(ep.flags);
				put_varuint(out, (uint64_t)ep.name);
				put_varuint(out, (uint64_t)ep.type);
			}
		}
	}
	return out;
}

/** 造一份清单落到 `<module_id 同目录同名的 .sig>`，析构时删除。 */
struct Stage {
	String module_id;
	String sig_path;
	// 写盘的实测结果。**不能只靠 `is_valid()` 静默跳过**：那样读取器一旦拿不到文件，
	// 用例会退化成"什么都没测到"却仍能通过"拒绝"那几条断言。
	Error open_error = OK;
	int64_t written_size = 0;

	Stage(const String &p_stem, const Vector<Member> &p_members, uint8_t p_version = 1) {
		// 扩展名宏**不含前导点**（`"js"` / `"sig"`），必须自己补 —— 读取器的推导式是
		// 「保留点、只丢扩展名字母」，两边拼法不一致会让夹具写到另一个文件名上，
		// 而用例自身的 `file_exists` 断言仍会通过（夹具自洽、与读取器不自洽）。
		const String js_name = p_stem + String(".") + String(JSB_JAVASCRIPT_EXT);
		const String sig_name = p_stem + String(".") + String(JSB_SIGNATURE_EXT);
		module_id = internal::settings::get_jsb_out_res_path().path_join(js_name);
		sig_path = internal::settings::get_jsb_out_res_path().path_join(sig_name);
		const Vector<uint8_t> bytes = serialize(p_members, p_version);
		const Ref<FileAccess> file = FileAccess::open(sig_path, FileAccess::WRITE);
		open_error = FileAccess::get_open_error();
		if (file.is_null() || open_error != OK) {
			return;
		}
		file->store_buffer(bytes.ptr(), bytes.size());
		written_size = (int64_t)bytes.size();
	}

	~Stage() {
		DirAccess::remove_absolute(sig_path);
	}
};

} //namespace signature_fixture

TEST_CASE("[runtime] [jsb.signature] sidecar reader: pool, overloads, optional and rest params") {
	using namespace signature_fixture;
	GodotJSScriptLanguageIniter initer;

	Vector<Member> members;
	{
		Member method;
		method.kind = 0;
		method.name = "add";
		// 两个重载：一个必填单参、一个可选 + 剩余参数。
		{
			Signature first;
			first.return_type = "number";
			first.params.push_back({ "a", "number", false });
			method.signatures.push_back(first);
		}
		{
			Signature second;
			second.return_type = "int32";
			second.is_vararg = true;
			second.params.push_back({ "a", "number", false });
			second.params.push_back({ "b", "float64", true });
			method.signatures.push_back(second);
		}
		members.push_back(method);

		Member signal;
		signal.kind = 1;
		signal.name = "changed";
		{
			Signature signature;
			signature.return_type = "void"; // 信号恒 void
			signature.params.push_back({ "value", "StringName", false });
			signal.signatures.push_back(signature);
		}
		members.push_back(signal);
	}
	Stage stage("test_signature_reader", members);
	// 夹具先自证：写盘失败会让下面的读取器断言全部失去意义（读取器返回 false 同样"符合预期"）。
	REQUIRE(stage.open_error == OK);
	REQUIRE(stage.written_size > 0);
	REQUIRE(FileAccess::file_exists(stage.sig_path));

	HashMap<StringName, LocalVector<ScriptMethodSignature>> methods;
	HashMap<StringName, LocalVector<PropertyInfo>> signals;
	REQUIRE(internal::signature_load(stage.module_id, methods, signals));

	// 重载按清单顺序全部保留（运行期看不到重载，只能靠清单）。
	const LocalVector<ScriptMethodSignature> *overloads = methods.getptr(StringName("add"));
	REQUIRE(overloads != nullptr);
	REQUIRE(overloads->size() == 2);
	CHECK((*overloads)[0].arguments.size() == 1);
	CHECK((*overloads)[0].default_count == 0);
	CHECK(!(*overloads)[0].is_vararg);
	CHECK((*overloads)[1].arguments.size() == 2);
	CHECK((*overloads)[1].default_count == 1); // `b` 带默认值
	CHECK((*overloads)[1].is_vararg);

	// 信号只取参数表，返回值恒 void（NIL 且**不带** NIL_IS_VARIANT）。
	const LocalVector<PropertyInfo> *signal_args = signals.getptr(StringName("changed"));
	REQUIRE(signal_args != nullptr);
	REQUIRE(signal_args->size() == 1);
	CHECK((*signal_args)[0].name == StringName("value"));
	CHECK((*signal_args)[0].type == Variant::STRING_NAME);
}

TEST_CASE("[runtime] [jsb.signature] type mapping: aliases, exposed-name reverse lookup, void") {
	using namespace signature_fixture;
	GodotJSScriptLanguageIniter initer;

	// 引擎别名：AST 上保留原文（checker 会退化成 number/string）⇒ 只能按原文识别。
	CHECK(internal::signature_resolve_type("int32", StringName("a"), true).type == Variant::INT);
	CHECK(internal::signature_resolve_type("uint64", StringName("a"), true).type == Variant::INT);
	CHECK(internal::signature_resolve_type("float64", StringName("a"), true).type == Variant::FLOAT);
	CHECK(internal::signature_resolve_type("StringName", StringName("a"), true).type == Variant::STRING_NAME);
	CHECK(internal::signature_resolve_type("number", StringName("a"), true).type == Variant::FLOAT);
	CHECK(internal::signature_resolve_type("string", StringName("a"), true).type == Variant::STRING);
	CHECK(internal::signature_resolve_type("boolean", StringName("a"), true).type == Variant::BOOL);

	// Variant 内建类型名（反向表现构，不硬编码）。
	CHECK(internal::signature_resolve_type("Vector2", StringName("a"), true).type == Variant::VECTOR2);

	// **反查原始类名**：`GArray` / `Array` 都必须落到 ARRAY —— 前者只有先经
	// `get_original_name` 还原才命中，后者是恒等映射。两个都断言才能证明反查这一步真的在跑
	// （只测其中一个时，换个 camel_case 设置就会有一个恒真、一个恒假，测不出回归）。
	CHECK(internal::signature_resolve_type("Array", StringName("a"), true).type == Variant::ARRAY);
	CHECK(internal::signature_resolve_type("GArray", StringName("a"), true).type == Variant::ARRAY);

	// Godot 类 ⇒ OBJECT + **原始类名**（不先反查就会写进暴露名，inspector 显示不一致）。
	{
		const PropertyInfo info = internal::signature_resolve_type("Node", StringName("a"), true);
		CHECK(info.type == Variant::OBJECT);
		CHECK(info.class_name == StringName("Node"));
	}

	// 不可映射（接口名 / 泛型实参）⇒ 参数位 NIL 带 NIL_IS_VARIANT（GDScript 解释为任意值）。
	{
		const PropertyInfo info = internal::signature_resolve_type("MyThing", StringName("a"), true);
		CHECK(info.type == Variant::NIL);
		CHECK((info.usage & PROPERTY_USAGE_NIL_IS_VARIANT) != 0);
	}
	// 无标注与参数位 `void` 同样是"任意值"。
	CHECK((internal::signature_resolve_type("", StringName("a"), true).usage & PROPERTY_USAGE_NIL_IS_VARIANT) != 0);

	// 返回值位：真 void ⇒ NIL 且**不带** NIL_IS_VARIANT（取它的返回值会报错）；
	// 不可映射 ⇒ NIL **带**该 flag（按任意值放过）。这两种语义不能共用一个 NIL。
	{
		const PropertyInfo void_return = internal::signature_resolve_type("void", StringName("m"), false);
		CHECK(void_return.type == Variant::NIL);
		CHECK((void_return.usage & PROPERTY_USAGE_NIL_IS_VARIANT) == 0);
	}
	{
		const PropertyInfo unmapped_return = internal::signature_resolve_type("MyThing", StringName("m"), false);
		CHECK(unmapped_return.type == Variant::NIL);
		CHECK((unmapped_return.usage & PROPERTY_USAGE_NIL_IS_VARIANT) != 0);
	}
}

TEST_CASE("[runtime] [jsb.signature] method info encoding: default_arguments and VARARG") {
	using namespace signature_fixture;
	GodotJSScriptLanguageIniter initer;

	ScriptMethodSignature signature;
	signature.is_vararg = true;
	signature.default_count = 1;
	signature.arguments.push_back(PropertyInfo(Variant::FLOAT, StringName("a")));
	signature.arguments.push_back(PropertyInfo(Variant::FLOAT, StringName("b")));
	// 真 void：不带 NIL_IS_VARIANT
	signature.return_val = PropertyInfo(Variant::NIL, StringName(), PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_DEFAULT);

	const MethodInfo info = internal::signature_to_method_info(StringName("add"), signature);
	CHECK(info.name == StringName("add"));
	// 剩余参数不进 arguments，只置 VARARG（否则 `add(1, 2, 3)` 会被判 Too many arguments）。
	CHECK(info.arguments.size() == 2);
	CHECK((info.flags & GDEXTENSION_METHOD_FLAG_VARARG) != 0);
	// 可选参数走 default_arguments：值取不到（TS 默认值是表达式），但**个数**决定最小 arity。
	REQUIRE(info.default_arguments.size() == 1);
	CHECK(info.default_arguments[0].get_type() == Variant::NIL);
	CHECK((info.return_val.usage & PROPERTY_USAGE_NIL_IS_VARIANT) == 0);
}

TEST_CASE("[runtime] [jsb.signature] malformed or obsolete manifests are rejected, not guessed at") {
	using namespace signature_fixture;
	GodotJSScriptLanguageIniter initer;

	Vector<Member> members;
	{
		Member method;
		method.kind = 0;
		method.name = "m";
		method.signatures.push_back({ "void", false, {} });
		members.push_back(method);
	}
	HashMap<StringName, LocalVector<ScriptMethodSignature>> methods;
	HashMap<StringName, LocalVector<PropertyInfo>> signals;

	// 版本不符：整份拒绝（旧版清单按"没有清单"处理，退回函数源文本扫描），不做半信半疑的解析。
	{
		Stage stage("test_signature_obsolete", members, 2);
		CHECK(!internal::signature_load(stage.module_id, methods, signals));
		CHECK(methods.is_empty());
		CHECK(signals.is_empty());
	}

	// 清单不存在：同样是"没有清单"，不是错误。
	{
		HashMap<StringName, LocalVector<ScriptMethodSignature>> absent_methods;
		HashMap<StringName, LocalVector<PropertyInfo>> absent_signals;
		CHECK(!internal::signature_load(StringName("res://.godot/godotjs_ext/__no_such_manifest__.js"), absent_methods, absent_signals));
	}

	// 合法清单：再确认一次上面两次拒绝不是"什么都没读到"造成的假象。
	{
		Stage stage("test_signature_valid", members);
		CHECK(internal::signature_load(stage.module_id, methods, signals));
		CHECK(methods.has(StringName("m")));
	}
}

} //namespace jsb::tests
