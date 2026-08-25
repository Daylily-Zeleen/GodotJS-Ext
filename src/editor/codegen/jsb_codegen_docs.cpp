/************************************************************************/
/*  jsb_codegen_docs.cpp                                                */
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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the       */
/*  GNU Lesser General Public License for more details.                 */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#include "jsb_codegen_docs.h"

#include <memory>

#include <runtime/internal/jsb_macros.h>
#include <runtime/internal/jsb_naming_util.h>
#include "jsb_string_names.h"

#include "api_tool/api_tool.h"
#include <api_tool/editor/api_tool_editor.h>

namespace jsb {
namespace codegen {

void DocCache::destroy() {
    for (const KeyValue<String, ClassDocEntry *> &E : docs_) {
        memdelete(E.value);
    }
    docs_.clear();
    missing_docs_.clear();
}

const ClassDocEntry *DocCache::find_doc(const String &p_class_name, const String &p_original_name) {
    if (p_class_name.is_empty()) {
        return nullptr;
    }

    // TS: `if (typeof class_doc === "object") return class_doc;`
    if (ClassDocEntry *const *cached = docs_.getptr(p_class_name)) {
        return *cached;
    }
    // TS: negative cache (`false` entries)
    if (missing_docs_.has(p_class_name)) {
        return nullptr;
    }

    const StringName original_name(p_original_name);

    ClassDocEntry *entry = nullptr;
    if (original_name == StringName("@GlobalScope")) {
        entry = memnew(ClassDocEntry);
        // The @GlobalScope pseudo-class document is assembled on the fly:
        // global constants + enum values (as constants, following godot's doc
        // format) and utility function descriptions.
        for (const StringName &constant_name : api_tool::list_global_constants()) {
            if (const auto constant_doc = api_tool::find_global_constant_document(constant_name)) {
                entry->constants.insert(internal::NamingUtil::get_constant_name(constant_doc->name),
                        constant_doc->description);
            }
        }
        for (const StringName &enum_name : api_tool::list_global_enums()) {
            if (const auto enum_doc = api_tool::find_global_enum_document(enum_name)) {
                for (const api_tool::ApiEnumValueDocument &enum_value_doc : enum_doc->values) {
                    entry->constants.insert(internal::NamingUtil::get_constant_name(enum_value_doc.name),
                            enum_value_doc.description);
                }
            }
        }
        for (const StringName &func_name : api_tool::list_utility_functions()) {
            if (const auto func_doc = api_tool::find_utility_function_document(func_name)) {
                entry->methods.insert(internal::NamingUtil::get_member_name(func_doc->name), func_doc->description);
            }
        }
    } else if (std::unique_ptr<api_tool::ApiClassDocument> doc = api_tool::find_document(original_name)) {
        entry = memnew(ClassDocEntry);
        entry->doc.brief_description = doc->brief_description;
        for (const api_tool::ApiConstantDocument &constant : doc->constants) {
            entry->constants.insert(internal::NamingUtil::get_constant_name(constant.name), constant.description);
        }
        // Built-in classes merge their enum values into the constants map
        // (godot's doc format), mirroring `_get_class_doc`.
        for (const api_tool::ApiEnumDocument &enum_doc : doc->enums) {
            for (const api_tool::ApiEnumValueDocument &enum_value_doc : enum_doc.values) {
                entry->constants.insert(internal::NamingUtil::get_constant_name(enum_value_doc.name),
                        enum_value_doc.description);
            }
        }
        for (const api_tool::ApiMethodDocument &method : doc->methods) {
            entry->methods.insert(internal::NamingUtil::get_member_name(method.name), method.description);
        }
        for (const api_tool::ApiPropertyDocument &property : doc->properties) {
            entry->properties.insert(internal::NamingUtil::get_member_name(property.name), property.description);
        }
    }

    if (entry == nullptr) {
        missing_docs_.insert(p_class_name);
        return nullptr;
    }

    docs_.insert(p_class_name, entry);
    return entry;
}

void destroy_doc_cache(DocCache &p_cache) {
    p_cache.destroy();
}

} // namespace codegen
} // namespace jsb
