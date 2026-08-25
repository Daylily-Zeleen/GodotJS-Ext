/************************************************************************/
/*  jsb_codegen_writer.h                                                */
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

// jsb_codegen_writer.h
// C++ port of the writer hierarchy in jsb.editor.codegen.ts:
// AbstractWriter / BufferingWriter / IndentWriter / ModuleWriter /
// NamespaceWriter / ClassWriter / EnumWriter / InterfaceWriter / ObjectWriter /
// PropertyWriter / GenericWriter / FileWriter / FileSplitter /
// TypeDescriptorWriter plus DocCommentHelper.
//
// The TS implementation passes writers by reference while buffering into
// parents; here every writer holds a pointer to its base writer and `finish()`
// flushes buffered lines into it (identical output shape).

#include "jsb_codegen_defs.h"
#include "jsb_codegen_docs.h"
#include "jsb_codegen_mutations.h"
#include "jsb_codegen_type_db.h"

#include <api_tool/editor/api_tool_doc_types.h>

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/templates/pair.hpp>
#include <godot_cpp/templates/vector.hpp>

#include <cstring>

namespace jsb {
namespace codegen {

class Writer;
class TypeDescriptorWriter;

// ---------------------------------------------------------------------------
// DocCommentHelper
// ---------------------------------------------------------------------------
class DocCommentHelper {
public:
    // writes `/** ... */` from a raw (unsimplified) description; returns true when written
    static bool write(Writer &r_writer, const String &p_description, bool p_newline);
    static bool write_lines(Writer &r_writer, const Vector<String> &p_lines, bool p_newline);

    static String get_simplified_description(const String &p_text);

private:
    static String get_leading_tab(const String &p_text);
    static String trim_leading_tab(const String &p_text, const String &p_leading_tab);
    static bool is_empty_or_whitespace(const String &p_text);
    static String replace_markup_content(const String &p_text, int p_from_pos, const String &p_markup, const String &p_rep);
    static String remove_markup_content(const String &p_text, int p_from_pos, const String &p_begin, const String &p_end);
};

// Description.forClass equivalent: brief description + "@link" suffix.
String make_class_description(const api_tool::ApiClassDocument *p_doc, const String &p_class_name);

// ---------------------------------------------------------------------------
// Writer (AbstractWriter)
// ---------------------------------------------------------------------------
class Writer {
public:
    virtual ~Writer() = default;

    virtual void line(const String &p_text) = 0;
    virtual void concatenate(const String &p_text) = 0;
    void append(bool p_newline, const String &p_text) {
        if (p_newline) {
            line(p_text);
        } else {
            concatenate(p_text);
        }
    }

    virtual int get_size() const = 0;
    virtual int get_lineno() const = 0;

    virtual void add_import(const String &p_preferred_name, const String &p_script_resource,
            const String &p_export_name = "default") = 0;

    virtual TypeDB *get_types() const = 0;

    // scope helpers; returned writers are heap-allocated and NOT owned by the
    // base writer - callers must `delete` them after `finish()`.
    class EnumWriter *enum_(const String &p_name);
    class NamespaceWriter *namespace_(const String &p_name, const ClassDocEntry *p_doc = nullptr);
    class InterfaceWriter *interface_(const String &p_name, const HashMap<String, GenericParameter> *p_generics = nullptr,
            const String &p_super = String(), const Vector<String> &p_super_generic_arguments = Vector<String>(),
            const Vector<String> *p_intro = nullptr);
    class ClassWriter *class_(struct ClassEmitOptions &p_options);
    class ObjectWriter *object_();
    class PropertyWriter *property_(const String &p_name);
    class GenericWriter *generic_(const String &p_name);

    void line_comment_(const String &p_text) { line("// " + p_text); }

    // EnumWriter resolves the enclosing class document through this hook
    // (TS: `this._base.class_doc`); ClassWriter overrides it.
    virtual const ClassDocEntry *get_class_doc() const { return nullptr; }

protected:
    virtual Writer *get_class_doc_source() const { return nullptr; }
};

// options for Writer::class_ (mirrors the long TS constructor parameter list)
struct ClassEmitOptions {
    String name;
    String original_name; // engine-side name for doc lookup ("" = same as name)
    // insertion-ordered (TS Record preserves key order; HashMap would shuffle
    // the rendered <T extends ...> list)
    LocalVector<Pair<String, GenericParameter>> generic_parameters;
    String super; // empty = none
    Vector<String> super_generic_arguments;
    LocalVector<ImplementsEntry> implements;
    Vector<String> intro;
    Vector<String> prelude;
    const TypeMutation *mutation = nullptr; // for property_overrides lookup
    bool singleton_mode = false;
    String class_doc; // raw description ("" = none)
};

// ---------------------------------------------------------------------------
// BufferingWriter
// ---------------------------------------------------------------------------
class BufferingWriter : public Writer {
public:
    explicit BufferingWriter(Writer *p_base) : base_(p_base) {}

    int get_size() const override { return size_; }
    int get_lineno() const override { return lines_.size(); }
    TypeDB *get_types() const override { return base_->get_types(); }

    void add_import(const String &p_preferred_name, const String &p_script_resource,
            const String &p_export_name = "default") override {
        base_->add_import(p_preferred_name, p_script_resource, p_export_name);
    }

    void line(const String &p_text) override {
        lines_.push_back(p_text);
        size_ += buffered_size(p_text, lines_.size() > 1);
    }

    void concatenate(const String &p_text) override {
        if (!lines_.is_empty()) {
            const int last = lines_.size() - 1;
            lines_.write[last] = lines_[last] + p_text;
            size_ += buffered_size(p_text, false);
        } else {
            line(p_text);
        }
    }

protected:
    virtual int buffered_size(const String &p_text, bool p_new_line) const = 0;

    Writer *base_;
    Vector<String> lines_;
    int size_ = 0;
};

// ---------------------------------------------------------------------------
// IndentWriter
// ---------------------------------------------------------------------------
class IndentWriter : public BufferingWriter {
public:
    explicit IndentWriter(Writer *p_base, bool p_always_multiline = false, bool p_indent_first_line = true)
            : BufferingWriter(p_base), never_collapse_(p_always_multiline), indent_first_line_(p_indent_first_line) {}

    virtual void finish();

protected:
    int buffered_size(const String &p_text, bool p_new_line) const override {
        return p_text.length() + ((int)lines_.size() > 1 || indent_first_line_ ? (int)strlen(kTab) : 0) + (p_new_line ? 1 : 0);
    }

    bool never_collapse_;
    bool indent_first_line_;
};

// ---------------------------------------------------------------------------
// ModuleWriter
// ---------------------------------------------------------------------------
class ModuleWriter final : public IndentWriter {
public:
    ModuleWriter(Writer *p_base, const String &p_name) : IndentWriter(p_base, true), name_(p_name) {}

    // emits imports + `declare module "<name>" { ... }`
    void finish();

    // godot utility functions must be in global scope (inside the module decl
    // they are still declared as plain `function`)
    void utility_(const MethodDecl &p_method);

private:
    String name_;
};

// ---------------------------------------------------------------------------
// NamespaceWriter
// ---------------------------------------------------------------------------
class NamespaceWriter final : public IndentWriter {
public:
    // TS NamespaceWriter carries the enclosing class document so enum elements
    // can resolve their constant descriptions (`this._base.class_doc`).
    NamespaceWriter(Writer *p_base, const String &p_name, const ClassDocEntry *p_doc = nullptr)
            : IndentWriter(p_base, true), name_(p_name), doc_(p_doc) {}

    void finish();

protected:
    const ClassDocEntry *get_class_doc() const override { return doc_; }

private:
    String name_;
    const ClassDocEntry *doc_ = nullptr;
};

// ---------------------------------------------------------------------------
// ClassWriter
// ---------------------------------------------------------------------------
class ClassWriter final : public IndentWriter {
public:
    ClassWriter(Writer *p_base, const ClassEmitOptions &p_options);

    void finish();

    void primitive_constant_(const PrimitiveConstantDecl &p_constant);
    void constant_(const ConstantDecl &p_constant);
    void property_(const PropertySetGetDecl &p_info, bool p_static_property);
    void primitive_property_(const PrimitiveGetSetDecl &p_info);
    void constructor_(const ConstructorDecl &p_info);
    void constructor_ex_();
    void operator_(const OperatorDecl &p_info);
    void virtual_method_(const MethodDecl &p_method) { method_(p_method, "/* gdvirtual */ "); }
    void ordinary_method_(const MethodDecl &p_method) { method_(p_method, ""); }
    void signal_(const SignalDecl &p_signal);

    // plain property writer (used for __godotRPCMap/__godotNameMap)
    class PropertyWriter *property_(const String &p_name);

private:
    void method_(const MethodDecl &p_method, const String &p_category);
    void intro();
    String head() const;
    String make_method_prefix(const MethodDecl &p_method) const {
        return (options_.singleton_mode || p_method.is_static) ? "static " : "";
    }
    const PropertyOverride *find_override(const String &p_name) const;

protected:
    // TS: enum elements read `this._base.class_doc?.constants[...]`
    const ClassDocEntry *get_class_doc() const override { return docs_; }

    ClassEmitOptions options_;
    TypeDB *types_;
    const ClassDocEntry *docs_; // TS: `this._doc` (resolved in the constructor)
    bool separator_line_ = false;
};

// ---------------------------------------------------------------------------
// EnumWriter
// ---------------------------------------------------------------------------
class EnumWriter final : public IndentWriter {
public:
    EnumWriter(Writer *p_base, const String &p_name) : IndentWriter(p_base, true), name_(p_name) {}

    EnumWriter *auto_finish() {
        auto_ = true;
        return this;
    }

    void finish();
    void element_(const String &p_name, int64_t p_value);

private:
    String name_;
    bool auto_ = false;
    bool separator_line_ = false;
};

// ---------------------------------------------------------------------------
// InterfaceWriter
// ---------------------------------------------------------------------------
class InterfaceWriter final : public IndentWriter {
public:
    InterfaceWriter(Writer *p_base, const String &p_name, const HashMap<String, GenericParameter> *p_generics,
            const String &p_super, const Vector<String> &p_super_generic_arguments, const Vector<String> *p_intro)
            : IndentWriter(p_base, true),
              name_(p_name),
              generics_(p_generics),
              super_(p_super),
              super_generic_arguments_(p_super_generic_arguments),
              intro_(p_intro) {}

    explicit InterfaceWriter(Writer *p_base, const String &p_name)
            : IndentWriter(p_base, true), name_(p_name) {}

    void finish();

    void property_(const String &p_key, const String &p_type);
    class PropertyWriter *property_(const String &p_key) { return base_->property_(p_key); }

private:
    String head() const;
    void intro();

    String name_;
    const HashMap<String, GenericParameter> *generics_ = nullptr;
    String super_;
    Vector<String> super_generic_arguments_;
    const Vector<String> *intro_ = nullptr;
};

// ---------------------------------------------------------------------------
// GenericWriter
// ---------------------------------------------------------------------------
class GenericWriter final : public IndentWriter {
public:
    GenericWriter(Writer *p_base, const String &p_name)
            : IndentWriter(p_base), name_(p_name) {
        size_ += p_name.length() + 2;
    }

    void finish();

private:
    String name_;
};

// ---------------------------------------------------------------------------
// ObjectWriter
// ---------------------------------------------------------------------------
class ObjectWriter final : public IndentWriter {
public:
    explicit ObjectWriter(Writer *p_base) : IndentWriter(p_base) {}

    void finish();
    void property_(const String &p_key, const String &p_type);
    class PropertyWriter *property_(const String &p_key) { return base_->property_(p_key); }
};

// ---------------------------------------------------------------------------
// PropertyWriter
// ---------------------------------------------------------------------------
class PropertyWriter final : public BufferingWriter {
public:
    PropertyWriter(Writer *p_base, const String &p_name, bool p_static_property = false,
            bool p_concatenate_first_line = false)
            : BufferingWriter(p_base),
              concatenate_first_line_(p_concatenate_first_line),
              key_(p_name),
              static_property_(p_static_property) {
        size_ += key_.length() + 3;
    }

    void finish();

protected:
    int buffered_size(const String &p_text, bool p_new_line) const override {
        return p_text.length() + (p_new_line ? 1 : 0);
    }

private:
    bool concatenate_first_line_;
    String key_;
    bool static_property_;
};

// ---------------------------------------------------------------------------
// FileWriter
// ---------------------------------------------------------------------------
class FileWriter final : public Writer {
public:
    FileWriter(const String &p_path, TypeDB *p_types, Ref<FileAccess> p_file)
            : path_(p_path), types_(p_types), file_(p_file) {}

    void line(const String &p_text) override;
    void concatenate(const String &p_text) override;
    int get_size() const override { return size_; }
    int get_lineno() const override { return lineno_; }
    TypeDB *get_types() const override { return types_; }

    void add_import(const String &p_preferred_name, const String &p_script_resource,
            const String &p_export_name = "default") override;

    void finish() { file_->flush(); }

    const String &get_path() const { return path_; }

    const Vector<String> &ordered_import_resources() const { return ordered_resources_; }
    const HashMap<String, String> &imports_for(const String &p_resource) const;
    String resolve_import(const String &p_destination) const;

private:
    String get_import_name(const String &p_preferred_name);

    String path_;
    TypeDB *types_ = nullptr;
    Ref<FileAccess> file_;
    int size_ = 0;
    int lineno_ = 0;

    HashMap<String, HashMap<String, String>> import_map_; // resource -> export_name -> preferred
    Vector<String> ordered_resources_; // insertion order of resources
    HashSet<String> import_names_;
};

// ---------------------------------------------------------------------------
// FileSplitter
// ---------------------------------------------------------------------------
class FileSplitter final {
public:
    FileSplitter(TypeDB *p_types, const String &p_path);

    void close();
    ModuleWriter *get_writer() const { return toplevel_; }
    int get_size() const;
    int get_lineno() const;

private:
    Ref<FileAccess> file_;
    ModuleWriter *toplevel_ = nullptr;
};

// ---------------------------------------------------------------------------
// TypeDescriptorWriter
// ---------------------------------------------------------------------------
class TypeDescriptorWriter final : public BufferingWriter {
public:
    explicit TypeDescriptorWriter(Writer *p_base, bool p_concatenate_first_line = false)
            : BufferingWriter(p_base), concatenate_first_line_(p_concatenate_first_line) {}

    void finish();
    void serialize_type_descriptor(const Dictionary &p_descriptor);

protected:
    int buffered_size(const String &p_text, bool p_new_line) const override {
        return p_text.length() + (p_new_line ? 1 : 0);
    }

    bool concatenate_first_line_;
};

} // namespace codegen
} // namespace jsb
