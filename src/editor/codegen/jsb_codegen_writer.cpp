/************************************************************************/
/*  jsb_codegen_writer.cpp                                              */
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

#include "jsb_codegen_writer.h"

#include "jsb_codegen_docs.h"

#include "api_tool/api_tool.h"
#include <api_tool/editor/api_tool_editor.h>

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <internal/jsb_naming_util.h>
#include <internal/jsb_settings.h>

namespace jsb {
namespace codegen {

using internal::NamingUtil;

// ---------------------------------------------------------------------------
// DocCommentHelper
// ---------------------------------------------------------------------------
String DocCommentHelper::get_leading_tab(const String &p_text) {
	String tab;
	for (int i = 0; i < p_text.length(); ++i) {
		if (p_text[i] != '\t') {
			break;
		}
		tab += "\t";
	}
	return tab;
}

String DocCommentHelper::trim_leading_tab(const String &p_text, const String &p_leading_tab) {
	if (!p_leading_tab.is_empty() && p_text.begins_with(p_leading_tab)) {
		return p_text.substr(p_leading_tab.length());
	}
	return p_text;
}

bool DocCommentHelper::is_empty_or_whitespace(const String &p_text) {
	for (int i = 0; i < p_text.length(); ++i) {
		if (p_text[i] != ' ' && p_text[i] != '\t') {
			return false;
		}
	}
	return true;
}

String DocCommentHelper::replace_markup_content(const String &p_text, int p_from_pos, const String &p_markup, const String &p_rep) {
	const int index = p_text.find(p_markup, p_from_pos);
	if (index >= 0) {
		return replace_markup_content(
				p_text.substr(0, index) + p_rep + p_text.substr(index + p_markup.length()),
				index + p_rep.length(),
				p_markup,
				p_rep);
	}
	return p_text;
}

String DocCommentHelper::remove_markup_content(const String &p_text, int p_from_pos, const String &p_begin, const String &p_end) {
	const int start = p_text.find(p_begin, p_from_pos);
	if (start >= 0) {
		const int end = p_text.find(p_end, p_from_pos);
		if (end >= 0) {
			return remove_markup_content(
					p_text.substr(0, start) + p_text.substr(end + p_end.length()), start, p_begin, p_end);
		}
	}
	return p_text;
}

String DocCommentHelper::get_simplified_description(const String &p_text) {
	String text = p_text;
	// get rid of all codeblocks since they are too long to read
	text = remove_markup_content(text, 0, "[codeblocks]", "[/codeblocks]");
	text = remove_markup_content(text, 0, "[codeblock]", "[/codeblock]");
	text = replace_markup_content(text, 0, "[code]", "`");
	text = replace_markup_content(text, 0, "[/code]", "`");
	text = replace_markup_content(text, 0, "[b]Note:[/b]", "  \n**Note:**");
	text = replace_markup_content(text, 0, "[b]", "**");
	text = replace_markup_content(text, 0, "[/b]", "**");
	text = replace_markup_content(text, 0, "[i]", " *");
	text = replace_markup_content(text, 0, "[/i]", "* ");
	if (*kVersionDocsUrl != '\0') {
		text = replace_markup_content(text, 0, "$DOCS_URL", kVersionDocsUrl);
	}
	return text;
}

bool DocCommentHelper::write_lines(Writer &r_writer, const Vector<String> &p_lines, bool p_newline) {
	if (p_lines.is_empty()) {
		return false;
	}
	Vector<String> lines = p_lines;
	if (!lines.is_empty() && is_empty_or_whitespace(lines[0])) {
		lines.remove_at(0);
	}
	if (!lines.is_empty() && is_empty_or_whitespace(lines[lines.size() - 1])) {
		lines.remove_at(lines.size() - 1);
	}
	if (lines.is_empty()) {
		return false;
	}
	const String leading_tab = get_leading_tab(lines[0]);
	for (int i = 0; i < lines.size(); ++i) {
		lines.write[i] = trim_leading_tab(lines[i], leading_tab);
	}
	if (p_newline) {
		r_writer.line("");
	}
	if (lines.size() == 1) {
		r_writer.line("/** " + lines[0] + " */");
		return true;
	}
	for (int i = 0; i < lines.size(); ++i) {
		if (i == 0) {
			r_writer.line("/** " + lines[i] + "  ");
		} else {
			r_writer.line(" *  " + lines[i] + "  ");
		}
	}
	r_writer.line(" */");
	return true;
}

bool DocCommentHelper::write(Writer &r_writer, const String &p_description, bool p_newline) {
	if (p_description.is_empty()) {
		return false;
	}
	String simplified = get_simplified_description(p_description);
	// Normalize CRLF and stray CR - an embedded newline would otherwise
	// survive the split below and break line buffering (it skips the
	// indentation applied by writer.line()).
	simplified = simplified.replace("\r\n", "\n").replace("\r", "\n");

	Vector<String> lines;
	const PackedStringArray parts = simplified.split("\n");
	for (const String &part : parts) {
		lines.push_back(part);
	}
	return write_lines(r_writer, lines, p_newline);
}

String make_class_description(const api_tool::ApiClassDocument *p_doc, const String &p_class_name) {
	if (p_doc == nullptr) {
		return String();
	}
	String description = p_doc->brief_description;
	// TS Description.forClass: the docs link is only added when a class doc exists.
	if (*kVersionDocsUrl != '\0' && !p_class_name.is_empty()) {
		description += "\n@link " + String(kVersionDocsUrl) + "/classes/class_" + p_class_name.to_lower() + ".html";
	}
	return description;
}

// ---------------------------------------------------------------------------
// Writer scope helpers
// ---------------------------------------------------------------------------
EnumWriter *Writer::enum_(const String &p_name) {
	if (p_name.contains(".")) {
		PackedStringArray layers = p_name.split(".");
		const String last = layers[layers.size() - 1];
		// namespace of all but the last layer
		String ns_name;
		for (int i = 0; i < layers.size() - 1; ++i) {
			if (i > 0) {
				ns_name += ".";
			}
			ns_name += layers[i];
		}
		NamespaceWriter *ns = namespace_(ns_name);
		return memnew(EnumWriter(ns, last))->auto_finish();
	}
	return memnew(EnumWriter(this, p_name));
}

NamespaceWriter *Writer::namespace_(const String &p_name, const ClassDocEntry *p_doc) {
	return memnew(NamespaceWriter(this, p_name, p_doc));
}

InterfaceWriter *Writer::interface_(const String &p_name, const HashMap<String, GenericParameter> *p_generics, const String &p_super, const Vector<String> &p_super_generic_arguments, const Vector<String> *p_intro) {
	return memnew(InterfaceWriter(this, p_name, p_generics, p_super, p_super_generic_arguments, p_intro));
}

ClassWriter *Writer::class_(ClassEmitOptions &p_options) {
	return memnew(ClassWriter(this, p_options));
}

ObjectWriter *Writer::object_() {
	return memnew(ObjectWriter(this));
}

PropertyWriter *Writer::property_(const String &p_name) {
	return memnew(PropertyWriter(this, p_name));
}

GenericWriter *Writer::generic_(const String &p_name) {
	return memnew(GenericWriter(this, p_name));
}

// ---------------------------------------------------------------------------
// IndentWriter
// ---------------------------------------------------------------------------
void IndentWriter::finish() {
	if (lines_.is_empty()) {
		return;
	}
	if (lines_.size() == 1 && !never_collapse_) {
		base_->concatenate(lines_[0]);
		return;
	}
	if (indent_first_line_) {
		base_->line(kTab + lines_[0]);
	} else {
		base_->line(lines_[0]);
	}
	for (int i = 1; i < lines_.size(); ++i) {
		base_->line(kTab + lines_[i]);
	}
}

// ---------------------------------------------------------------------------
// ModuleWriter
// ---------------------------------------------------------------------------
void ModuleWriter::finish() {
	// imports (FileWriter owns the map)
	// NOTE: resolved through the base writer chain; ModuleWriter itself has no
	// import map, mirroring the TS `this.get_imports()` delegation.
	struct ImportSink {
		FileWriter *file = nullptr;
	};

	// walk down to the FileWriter for imports + path resolution
	Writer *w = base_;
	FileWriter *file_writer = nullptr;
	while (w != nullptr) {
		file_writer = dynamic_cast<FileWriter *>(w);
		if (file_writer != nullptr) {
			break;
		}
		w = dynamic_cast<ModuleWriter *>(w) ? static_cast<ModuleWriter *>(w)->base_ : nullptr;
	}

	if (file_writer != nullptr) {
		// iterate in insertion order (FileWriter keeps an ordered resource list)
		for (const String &script_resource : file_writer->ordered_import_resources()) {
			const HashMap<String, String> &script_import_map = file_writer->imports_for(script_resource);

			String default_import;
			int explicit_count = script_import_map.size();
			if (script_import_map.has("default")) {
				default_import = script_import_map["default"];
				explicit_count -= 1;
			}

			const String resolved_path = file_writer->resolve_import(script_resource);
			const bool explicit_imports = explicit_count > 0;

			if (!default_import.is_empty()) {
				if (explicit_imports) {
					base_->line("import " + default_import + ", {");
				} else {
					base_->line("import " + default_import + " from \"" + resolved_path + "\";");
				}
			} else {
				base_->line("import " + default_import + ", {");
			}

			if (explicit_imports) {
				for (const KeyValue<String, String> &E : script_import_map) {
					if (E.key == "default") {
						continue;
					}
					if (E.key == E.value) {
						base_->line(String(kTab) + E.key + ",");
					} else {
						base_->line(String(kTab) + E.key + " as " + E.value + ",");
					}
				}
				base_->line("} from \"" + resolved_path + "\";");
			}
		}
	}

	base_->line("declare module \"" + name_ + "\" {");
	IndentWriter::finish();
	base_->line("}");
}

void ModuleWriter::utility_(const MethodDecl &p_method) {
	const String args = get_types()->make_args(p_method);
	const String rval = get_types()->make_return(p_method);

	String exposed_name = p_method.name;
	if (!keyword_replacement(exposed_name).is_empty()) {
		exposed_name = NamingUtil::get_member_name("godot_" + exposed_name);
	}

	if (!get_types()->is_valid_method_name(exposed_name)) {
		line("// [INVALID_NAME]: function " + exposed_name + "(" + args + "): " + rval);
		return;
	}
	line("function " + exposed_name + "(" + args + "): " + rval);
}

// ---------------------------------------------------------------------------
// NamespaceWriter
// ---------------------------------------------------------------------------
void NamespaceWriter::finish() {
	if (lines_.is_empty()) {
		return;
	}
	base_->line("namespace " + name_ + " {");
	IndentWriter::finish();
	base_->line("}");
}

// ---------------------------------------------------------------------------
// ClassWriter
// ---------------------------------------------------------------------------
ClassWriter::ClassWriter(Writer *p_base, const ClassEmitOptions &p_options)
		: IndentWriter(p_base, true), options_(p_options), types_(p_base->get_types()), docs_(types_ != nullptr ? types_->find_doc(options_.name, options_.original_name.is_empty() ? options_.name : options_.original_name) : nullptr) {
	// TS: `this._doc = class_doc;` - resolved once per class (lazily cached in TypeDB).
}

String ClassWriter::head() const {
	String params;
	if (!options_.generic_parameters.is_empty()) {
		params = "<";
		bool first = true;
		for (const Pair<String, GenericParameter> &gp : options_.generic_parameters) {
			if (!first) {
				params += ", ";
			}
			params += gp.first;
			if (!gp.second.extends_.is_empty()) {
				params += " extends " + gp.second.extends_;
			}
			if (!gp.second.default_.is_empty()) {
				params += " = " + gp.second.default_;
			}
			first = false;
		}
		params += ">";
	}

	String class_extends;
	if (!options_.super.is_empty()) {
		String args;
		if (!options_.super_generic_arguments.is_empty()) {
			args = "<";
			for (int i = 0; i < options_.super_generic_arguments.size(); ++i) {
				if (i > 0) {
					args += ", ";
				}
				args += options_.super_generic_arguments[i];
			}
			args += ">";
		}
		class_extends = " extends " + options_.super + args;
	}

	String class_implements;
	if (!options_.implements.is_empty()) {
		class_implements = " implements ";
		for (int i = 0; i < (int)options_.implements.size(); ++i) {
			if (i > 0) {
				class_implements += ", ";
			}
			const ImplementsEntry &entry = options_.implements[i];
			class_implements += entry.type;
			if (!entry.generic_arguments.is_empty()) {
				class_implements += "<";
				for (int j = 0; j < entry.generic_arguments.size(); ++j) {
					if (j > 0) {
						class_implements += ", ";
					}
					class_implements += entry.generic_arguments[j];
				}
				class_implements += ">";
			}
		}
	}

	return "class " + options_.name + params + class_extends + class_implements;
}

void ClassWriter::intro() {
	for (const String &l : options_.intro) {
		base_->line(String(kTab) + l);
	}
}

const PropertyOverride *ClassWriter::find_override(const String &p_name) const {
	if (options_.mutation == nullptr) {
		return nullptr;
	}
	const HashMap<String, PropertyOverride> &overrides = options_.mutation->property_overrides;
	const PropertyOverride *found = overrides.getptr(p_name);
	// camel-case bindings are not enabled by default in this project build; the
	// plain name lookup matches the TS behavior when CAMEL_CASE_BINDINGS is off.
	return found;
}

void ClassWriter::finish() {
	for (const String &l : options_.prelude) {
		base_->line(l);
	}
	// TS: `DocCommentHelper.write(this._base, Description.forClass(this.types, this._name), false);`
	const String class_description =
			docs_ != nullptr ? make_class_description(&docs_->doc, options_.name) : String();
	DocCommentHelper::write(*base_, class_description, false);
	base_->line(head() + " {");
	intro();
	IndentWriter::finish();
	base_->line("}");
}

void ClassWriter::primitive_constant_(const PrimitiveConstantDecl &p_constant) {
	DocCommentHelper::write(*this,
			docs_ != nullptr && docs_->constants.has(p_constant.name) ? docs_->constants[p_constant.name] : String(),
			separator_line_);
	separator_line_ = true;
	if (p_constant.has_literal_value) {
		line("static readonly " + name_string(p_constant.name) + " = " + itos(p_constant.literal_value));
	} else if (p_constant.literal_is_float) {
		line("static readonly " + name_string(p_constant.name) + " = " + String::num(p_constant.literal_float_value));
	} else if (p_constant.literal_is_bool) {
		line("static readonly " + name_string(p_constant.name) + " = " + (p_constant.literal_bool_value ? "true" : "false"));
	} else if (p_constant.literal_is_string) {
		line("static readonly " + name_string(p_constant.name) + " = '" + p_constant.literal_string_value + "'");
	} else {
		line("static readonly " + name_string(p_constant.name) + ": Readonly<" + types_->get_variant_to_name(p_constant.type) + ">");
	}
}

void ClassWriter::constant_(const ConstantDecl &p_constant) {
	DocCommentHelper::write(*this,
			docs_ != nullptr && docs_->constants.has(p_constant.name) ? docs_->constants[p_constant.name] : String(),
			separator_line_);
	separator_line_ = true;
	line("static readonly " + name_string(p_constant.name) + " = " + itos(p_constant.value));
}

void ClassWriter::property_(const PropertySetGetDecl &p_info, bool p_static_property) {
	DocCommentHelper::write(*this,
			docs_ != nullptr && docs_->properties.has(p_info.name) ? docs_->properties[p_info.name] : String(),
			separator_line_);
	separator_line_ = true;

	const PropertyOverride *override_ = find_override(p_info.name);
	if (override_ != nullptr && override_->is_literal) {
		for (const String &l : override_->literal_lines) {
			line(l);
		}
		return;
	}

	const String name = name_string(p_info.name);
	const String static_prefix = p_static_property ? "static " : "";

	auto emit = [&](const String &p_line) {
		if (override_ != nullptr) {
			line(override_->mutator(p_line));
		} else {
			line(p_line);
		}
	};

	// declare as get/set to avoid the pitfalls of modifying a value type return value
	emit(static_prefix + String("get ") + name + "(): " + types_->make_typename(p_info.info, false, false));
	if (!p_info.setter.is_empty()) {
		emit(static_prefix + String("set ") + name + "(value: " + types_->make_typename(p_info.info, true, false) + ")");
	}
}

void ClassWriter::primitive_property_(const PrimitiveGetSetDecl &p_info) {
	separator_line_ = true;
	const String name = name_string(p_info.name);
	line("get " + name + "(): " + types_->get_variant_to_name(p_info.type));
	line("set " + name + "(value: " + types_->primitive_type_name_as_input_public(p_info.type) + ")");
}

void ClassWriter::constructor_(const ConstructorDecl &p_info) {
	separator_line_ = true;
	String args;
	for (int i = 0; i < (int)p_info.arguments.size(); ++i) {
		if (i > 0) {
			args += ", ";
		}
		args += TypeDB::replace_var_name(p_info.arguments[i].name) + ": " + types_->primitive_type_name_as_input_public(p_info.arguments[i].type);
	}
	line("constructor(" + args + ")");
}

void ClassWriter::constructor_ex_() {
	line("constructor(identifier?: any)");
}

void ClassWriter::operator_(const OperatorDecl &p_info) {
	separator_line_ = true;
	const String return_type_name = types_->get_variant_to_name(p_info.return_type);
	const String left_type_name = types_->primitive_type_name_as_input_public(p_info.left_type);
	if (p_info.right_type == Variant::NIL) {
		line("static " + p_info.op_name + "(left: " + left_type_name + "): " + return_type_name);
	} else {
		const String right_type_name = types_->primitive_type_name_as_input_public(p_info.right_type);
		line("static " + p_info.op_name + "(left: " + left_type_name + ", right: " + right_type_name + "): " + return_type_name);
	}
}

void ClassWriter::method_(const MethodDecl &p_method, const String &p_category) {
	DocCommentHelper::write(*this,
			docs_ != nullptr && docs_->methods.has(p_method.name) ? docs_->methods[p_method.name] : String(),
			separator_line_);
	separator_line_ = true;

	const PropertyOverride *override_ = find_override(p_method.name);
	if (override_ != nullptr && override_->is_literal) {
		for (const String &l : override_->literal_lines) {
			line(l);
		}
		return;
	}

	const String args = types_->make_args(p_method);
	const String rval = types_->make_return(p_method);
	const String prefix = make_method_prefix(p_method);

	if (!types_->is_valid_method_name(p_method.name)) {
		line(p_category + prefix + "[\"" + p_method.name + "\"]: (" + args + ") => " + rval);
		return;
	}

	const String generated = p_category + prefix + name_string(p_method.name) + "(" + args + "): " + rval;
	if (override_ != nullptr) {
		line(override_->mutator(generated));
	} else {
		line(generated);
	}
}

void ClassWriter::signal_(const SignalDecl &p_signal) {
	DocCommentHelper::write(*this,
			docs_ != nullptr && docs_->methods.has(p_signal.name) ? docs_->methods[p_signal.name] : String(),
			separator_line_);
	separator_line_ = true;
	const String sig = types_->make_signal_type(p_signal);
	const String name = name_string(p_signal.name);
	if (options_.singleton_mode) {
		line("static readonly " + name + ": " + sig);
	} else {
		line("readonly " + name + ": " + sig);
	}
}

PropertyWriter *ClassWriter::property_(const String &p_name) {
	// TS: `new PropertyWriter(this, name)` - properties attach to the class body
	// itself (delegating to base_ would emit them at module scope).
	return memnew(PropertyWriter(this, p_name));
}

// ---------------------------------------------------------------------------
// EnumWriter
// ---------------------------------------------------------------------------
void EnumWriter::finish() {
	if (!lines_.is_empty()) {
		base_->line("enum " + name_ + " {");
		IndentWriter::finish();
		base_->line("}");
	}
	if (auto_) {
		// auto-finish cascades to the base writer (IndentWriter::finish is virtual via concrete type)
		static_cast<IndentWriter *>(base_)->finish();
	}
}

void EnumWriter::element_(const String &p_name, int64_t p_value) {
	// TS: enum elements pull their doc from the enclosing namespace's class doc
	// constants map (`this._base.class_doc?.constants[name]?.description`).
	const ClassDocEntry *doc = base_->get_class_doc();
	DocCommentHelper::write(*this,
			doc != nullptr && doc->constants.has(p_name) ? doc->constants[p_name] : String(),
			separator_line_);
	separator_line_ = true;
	line(p_name + String(" = ") + itos(p_value) + ",");
}

// ---------------------------------------------------------------------------
// InterfaceWriter
// ---------------------------------------------------------------------------
String InterfaceWriter::head() const {
	String params;
	if (generics_ != nullptr && !generics_->is_empty()) {
		params = "<";
		bool first = true;
		for (const KeyValue<String, GenericParameter> &E : *generics_) {
			if (!first) {
				params += ", ";
			}
			params += E.key;
			if (!E.value.extends_.is_empty()) {
				params += " extends " + E.value.extends_;
			}
			if (!E.value.default_.is_empty()) {
				params += " = " + E.value.default_;
			}
			first = false;
		}
		params += ">";
	}

	if (super_.is_empty()) {
		return "interface " + name_ + params;
	}
	String args;
	if (!super_generic_arguments_.is_empty()) {
		args = "<";
		for (int i = 0; i < super_generic_arguments_.size(); ++i) {
			if (i > 0) {
				args += ", ";
			}
			args += super_generic_arguments_[i];
		}
		args += ">";
	}
	return "interface " + name_ + params + " extends " + super_ + args;
}

void InterfaceWriter::intro() {
	if (intro_ == nullptr) {
		return;
	}
	for (const String &l : *intro_) {
		base_->line(String(kTab) + l);
	}
}

void InterfaceWriter::finish() {
	base_->line(head() + " {");
	intro();
	IndentWriter::finish();
	base_->line("}");
}

void InterfaceWriter::property_(const String &p_key, const String &p_type) {
	line(name_string(p_key) + ": " + p_type + ";");
}

// ---------------------------------------------------------------------------
// GenericWriter
// ---------------------------------------------------------------------------
void GenericWriter::finish() {
	if (lines_.size() < 2) {
		base_->line(name_ + "<" + (lines_.is_empty() ? "" : lines_[0]) + ">");
		return;
	}
	base_->line(name_ + "<");
	IndentWriter::finish();
	base_->line(">");
}

// ---------------------------------------------------------------------------
// ObjectWriter
// ---------------------------------------------------------------------------
void ObjectWriter::finish() {
	if (lines_.is_empty()) {
		base_->line("{}");
		return;
	}
	const int line_count = lines_.size();
	const bool single_line = line_count == 1 && !never_collapse_;
	const String padding = (line_count == 1 && single_line) ? " " : "";

	base_->line("{" + padding);
	IndentWriter::finish();
	base_->append(!single_line, padding + String("}"));
}

void ObjectWriter::property_(const String &p_key, const String &p_type) {
	line(name_string(p_key) + ": " + p_type + ";");
}

// ---------------------------------------------------------------------------
// PropertyWriter
// ---------------------------------------------------------------------------
void PropertyWriter::finish() {
	if (lines_.is_empty()) {
		return;
	}
	base_->append(!concatenate_first_line_,
			String(static_property_ ? "static " : "") + name_string(key_) + ": ");

	for (int i = 0; i < lines_.size(); ++i) {
		base_->append(i > 0, lines_[i]);
	}
	base_->concatenate(";");
}

// ---------------------------------------------------------------------------
// FileWriter
// ---------------------------------------------------------------------------
void FileWriter::line(const String &p_text) {
	file_->store_line(p_text);
	size_ += p_text.length() + 1;
	lineno_ += 1;
}

void FileWriter::concatenate(const String &p_text) {
	file_->store_string(p_text);
	size_ += p_text.length();
}

void FileWriter::add_import(const String &p_preferred_name, const String &p_script_resource, const String &p_export_name) {
	if (!import_map_.has(p_script_resource)) {
		import_map_.insert(p_script_resource, HashMap<String, String>());
		ordered_resources_.push_back(p_script_resource);
	}
	HashMap<String, String> &resource_imports = *import_map_.getptr(p_script_resource);
	if (!resource_imports.has(p_export_name)) {
		resource_imports.insert(p_export_name, get_import_name(p_preferred_name));
	}
}

String FileWriter::get_import_name(const String &p_preferred_name) {
	if (p_preferred_name.is_empty()) {
		return get_import_name("MyType");
	}
	if (import_names_.has(p_preferred_name)) {
		return get_import_name(p_preferred_name + String("_"));
	}
	import_names_.insert(p_preferred_name);
	return p_preferred_name;
}

const HashMap<String, String> &FileWriter::imports_for(const String &p_resource) const {
	static const HashMap<String, String> kEmpty;
	const HashMap<String, String> *it = import_map_.getptr(p_resource);
	return it ? *it : kEmpty;
}

String FileWriter::resolve_import(const String &p_destination) const {
	// source path normalized to res:// (mirrors the TS implementation)
	String source = path_;
	while (source.begins_with("./") || source.begins_with("/")) {
		source = source.substr(1);
	}
	source = "res://" + source;

	int last_slash_index = -1;
	const int max_check = MIN(source.length(), p_destination.length());
	for (int i = 0; i < max_check && source[i] == p_destination[i]; ++i) {
		if (source[i] == '/') {
			last_slash_index = i;
		}
	}

	String up;
	for (int i = last_slash_index + 1; i < source.length(); ++i) {
		if (source[i] == '/') {
			up += "../";
		}
	}

	String tail = p_destination.substr(last_slash_index + 1);
	// strip .ts/.js/.tsx/.jsx extension
	for (const char *ext : { ".ts", ".js", ".tsx", ".jsx" }) {
		if (tail.ends_with(ext)) {
			tail = tail.substr(0, tail.length() - (int)strlen(ext));
			break;
		}
	}
	return (up.is_empty() ? "./" : up) + tail;
}

// ---------------------------------------------------------------------------
// FileSplitter
// ---------------------------------------------------------------------------
FileSplitter::FileSplitter(TypeDB *p_types, const String &p_path) {
	file_ = FileAccess::open(p_path, FileAccess::WRITE);
	toplevel_ = memnew(ModuleWriter(memnew(FileWriter(p_path, p_types, file_)), "godot"));

	file_->store_line("// AUTO-GENERATED");
	file_->store_line("");
	file_->store_string(make_copyright_header(p_path.get_file()));
}

void FileSplitter::close() {
	toplevel_->finish();
	file_->flush();
	file_->close();
}

int FileSplitter::get_size() const { return toplevel_->BufferingWriter::get_size(); }
int FileSplitter::get_lineno() const { return toplevel_->BufferingWriter::get_lineno(); }

// ---------------------------------------------------------------------------
// TypeDescriptorWriter
// ---------------------------------------------------------------------------
void TypeDescriptorWriter::finish() {
	if (lines_.is_empty()) {
		return;
	}
	base_->append(!concatenate_first_line_, lines_[0]);
	for (int i = 1; i < lines_.size(); ++i) {
		base_->append(true, lines_[i]);
	}
}

void TypeDescriptorWriter::serialize_type_descriptor(const Dictionary &p_descriptor) {
	const int type = p_descriptor.get("type", (int)DescriptorType::Godot);

	switch ((DescriptorType)type) {
		case DescriptorType::Godot:
		case DescriptorType::User: {
			const String name = p_descriptor.get("name", "");
			const Array arguments = p_descriptor.get("arguments", Array());

			if (!name.is_empty() && !arguments.is_empty()) {
				line(name + String("<"));
				IndentWriter *indent = memnew(IndentWriter(this));
				TypeDescriptorWriter args(indent);
				for (int i = 0; i < arguments.size(); ++i) {
					if (i > 0) {
						args.concatenate(",");
					}
					args.serialize_type_descriptor(arguments[i]);
				}
				args.finish();
				indent->finish();
				append(indent->get_lineno() > 1, ">");
				memdelete(indent);
			} else {
				line(name);
			}

			if ((DescriptorType)type == DescriptorType::User) {
				add_import(name, p_descriptor.get("resource", ""), p_descriptor.get("export", "default"));
			}
			break;
		}

		case DescriptorType::FunctionLiteral: {
			const Array generics = p_descriptor.get("generics", Array());
			const int generic_count = generics.size();

			if (generic_count > 0) {
				concatenate("<");
				for (int i = 0; i < generic_count; ++i) {
					const Dictionary generic = generics[i];
					if (i > 0) {
						concatenate(", ");
						line(generic.get("name", ""));
					} else {
						append(generic_count == 1, generic.get("name", ""));
					}
					const Dictionary extends_desc = generic.get("extends", Dictionary());
					if (!extends_desc.is_empty()) {
						concatenate(" extends ");
						TypeDescriptorWriter extends_writer(this, true);
						extends_writer.serialize_type_descriptor(extends_desc);
						extends_writer.finish();
					}
					const Dictionary default_desc = generic.get("default", Dictionary());
					if (!default_desc.is_empty()) {
						concatenate(" = ");
						TypeDescriptorWriter default_writer(this, true);
						default_writer.serialize_type_descriptor(default_desc);
						default_writer.finish();
					}
				}
				concatenate(">");
			}

			append(generic_count == 0, "(");

			const Array parameters = p_descriptor.get("parameters", Array());
			if (!parameters.is_empty()) {
				IndentWriter *indent = memnew(IndentWriter(this));
				for (int i = 0; i < parameters.size(); ++i) {
					const Dictionary param = parameters[i];
					if (i > 0) {
						indent->concatenate(", ");
					}
					indent->line(String(param.get("name", "")) + (param.get("optional", false) ? "?" : "") + ": ");
					TypeDescriptorWriter param_writer(indent, true);
					param_writer.serialize_type_descriptor(param.get("type", Dictionary()));
					param_writer.finish();
				}
				indent->finish();
				memdelete(indent);
			}

			append(get_lineno() > 1, ") => ");

			const Dictionary returns = p_descriptor.get("returns", Dictionary());
			if (!returns.is_empty()) {
				IndentWriter *indent = memnew(IndentWriter(this, false));
				const int return_type = returns.get("type", (int)DescriptorType::Godot);
				const bool parenthesis_required = return_type == (int)DescriptorType::Union
						|| return_type == (int)DescriptorType::Intersection
						|| return_type == (int)DescriptorType::FunctionLiteral
						|| return_type == (int)DescriptorType::Conditional;

				if (parenthesis_required) {
					indent->line("(");
				}
				TypeDescriptorWriter return_writer(indent, parenthesis_required);
				return_writer.serialize_type_descriptor(returns);
				return_writer.finish();
				if (parenthesis_required) {
					indent->concatenate(")");
				}
				indent->finish();
				memdelete(indent);
			} else {
				concatenate("void");
			}
			break;
		}

		case DescriptorType::ObjectLiteral: {
			const Dictionary properties = p_descriptor.get("properties", Dictionary());
			const Dictionary index = p_descriptor.get("index", Dictionary());

			if (properties.is_empty() && index.is_empty()) {
				line("{}");
				break;
			}

			line("{");
			IndentWriter *indent = memnew(IndentWriter(this, true));
			Array keys = properties.keys();
			for (int i = 0; i < keys.size(); ++i) {
				const String key = keys[i];
				const Dictionary value = properties[key];
				if (value.is_empty()) {
					continue;
				}
				indent->line(name_string(key) + (value.get("optional", false) ? "?" : "") + ": ");
				TypeDescriptorWriter prop_writer(indent, true);
				prop_writer.serialize_type_descriptor(value);
				prop_writer.finish();
				indent->concatenate(";");
			}

			if (!index.is_empty()) {
				indent->line("[key: ");
				TypeDescriptorWriter key_writer(indent, true);
				key_writer.serialize_type_descriptor(index.get("key", Dictionary()));
				key_writer.finish();
				indent->concatenate("]: ");
				TypeDescriptorWriter value_writer(indent, true);
				value_writer.serialize_type_descriptor(index.get("value", Dictionary()));
				value_writer.finish();
				indent->concatenate(";");
			}

			indent->finish();
			memdelete(indent);
			line("}");
			break;
		}

		case DescriptorType::StringLiteral: {
			const String value = p_descriptor.get("value", "");
			if (p_descriptor.get("template", false)) {
				line("`" + value.replace("`", "\\`") + "`");
			} else {
				line("\"" + value.replace("\"", "\\\"") + "\"");
			}
			break;
		}

		case DescriptorType::NumericLiteral:
			line(String(p_descriptor.get("value", 0.0)));
			break;

		case DescriptorType::BooleanLiteral:
			line(p_descriptor.get("value", false) ? "true" : "false");
			break;

		case DescriptorType::Tuple: {
			const Array elements = p_descriptor.get("elements", Array());
			line("[");
			const bool multiline = elements.size() > 1;
			for (int i = 0; i < elements.size(); ++i) {
				const Dictionary element = elements[i];
				if (i > 0) {
					line(", ");
				}
				const String element_name = element.get("name", "");
				if (!element_name.is_empty()) {
					append(multiline, element_name + String(": "));
				}
				TypeDescriptorWriter tuple_writer(this, !multiline || !element_name.is_empty());
				tuple_writer.serialize_type_descriptor(element.get("type", Dictionary()));
				tuple_writer.finish();
			}
			append(multiline, "]");
			break;
		}

		case DescriptorType::Union:
		case DescriptorType::Intersection: {
			const Array types = p_descriptor.get("types", Array());
			const bool multiline = types.size() > 1;
			const bool is_union = (DescriptorType)type == DescriptorType::Union;
			// TS: `${multiline ? "" : " "}| ` / `${multiline ? "" : " "}& ` -
			// the ternary only picks the leading space, the operator suffix is
			// always appended ("| " on union lines, "& " before object members).
			const String separator = String(multiline ? "" : " ") + (is_union ? "| " : "& ");
			IndentWriter *members = memnew(IndentWriter(this, true, false));

			for (int i = 0; i < types.size(); ++i) {
				const Dictionary member_type_desc = types[i];
				if (i > 0) {
					members->line(separator);
				}
				const int member_type = member_type_desc.get("type", (int)DescriptorType::Godot);
				const bool parenthesis_required = member_type == (int)DescriptorType::Union
						|| member_type == (int)DescriptorType::Intersection
						|| member_type == (int)DescriptorType::FunctionLiteral
						|| member_type == (int)DescriptorType::Conditional;

				if (parenthesis_required) {
					members->append(i == 0, "(");
				}
				TypeDescriptorWriter member(members, parenthesis_required || i > 0);
				member.serialize_type_descriptor(member_type_desc);
				member.finish();
				if (parenthesis_required) {
					members->concatenate(")");
				}
			}

			members->finish();
			memdelete(members);
			break;
		}

		case DescriptorType::Infer:
			line("infer " + String(p_descriptor.get("name", "")));
			break;

		case DescriptorType::Conditional: {
			TypeDescriptorWriter check_writer(this);
			check_writer.serialize_type_descriptor(p_descriptor.get("check", Dictionary()));
			check_writer.finish();

			line("extends ");
			TypeDescriptorWriter extends_writer(this, true);
			extends_writer.serialize_type_descriptor(p_descriptor.get("extends", Dictionary()));
			extends_writer.finish();

			line("? ");
			TypeDescriptorWriter true_writer(this, true);
			true_writer.serialize_type_descriptor(p_descriptor.get("true", Dictionary()));
			true_writer.finish();

			line(": ");
			TypeDescriptorWriter false_writer(this, true);
			false_writer.serialize_type_descriptor(p_descriptor.get("false", Dictionary()));
			false_writer.finish();
			break;
		}

		case DescriptorType::Mapped: {
			line("{ [" + String(p_descriptor.get("key", "")) + " in ");
			TypeDescriptorWriter in_writer(this, true);
			in_writer.serialize_type_descriptor(p_descriptor.get("in", Dictionary()));
			in_writer.finish();

			const Dictionary as_desc = p_descriptor.get("as", Dictionary());
			if (!as_desc.is_empty()) {
				TypeDescriptorWriter as_writer(this, true);
				as_writer.serialize_type_descriptor(as_desc);
				as_writer.finish();
			}

			concatenate("]: ");

			const int value_start_line = get_lineno();
			TypeDescriptorWriter value_writer(this, true);
			value_writer.serialize_type_descriptor(p_descriptor.get("value", Dictionary()));
			value_writer.finish();
			append(get_lineno() == value_start_line, "}");
			break;
		}

		case DescriptorType::Indexed: {
			TypeDescriptorWriter base_writer(this);
			base_writer.serialize_type_descriptor(p_descriptor.get("base", Dictionary()));
			base_writer.finish();

			concatenate("[");
			TypeDescriptorWriter index_writer(this, true);
			index_writer.serialize_type_descriptor(p_descriptor.get("index", Dictionary()));
			index_writer.finish();
			concatenate("]");
			break;
		}
	}
}

} // namespace codegen
} // namespace jsb
