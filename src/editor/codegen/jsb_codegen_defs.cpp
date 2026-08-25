/************************************************************************/
/*  jsb_codegen_defs.cpp                                                */
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

#include "jsb_codegen_defs.h"

// provides `using namespace godot` (project-wide convention for extension code)
#include "runtime/compat/jsb_compat.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace jsb {
namespace codegen {

const char *const kPredefinedLines[] = {
    "type byte = number",
    "type int32 = number",
    "type int64 = number /* || bigint */",
    "type float32 = number",
    "type float64 = number",
    "type uint32 = number",
    "type StringName = string",
    "type unresolved = any",
};
const int kPredefinedLineCount = sizeof(kPredefinedLines) / sizeof(kPredefinedLines[0]);

const char *const kKeywordReplacements[][2] = {
    { "default", "default_" },
    { "let", "let_" },
    { "var", "var_" },
    { "const", "const_" },
    { "of", "of_" },
    { "for", "for_" },
    { "in", "in_" },
    { "out", "out_" },
    { "with", "with_" },
    { "break", "break_" },
    { "else", "else_" },
    { "enum", "enum_" },
    { "class", "class_" },
    { "string", "string_" },
    { "Symbol", "Symbol_" },
    { "typeof", "typeof_" },
    { "arguments", "arguments_" },
    { "function", "function_" },
    // a special item which used as the name of variadic arguments placement
    { "varargs", "varargs_" },
};
const int kKeywordReplacementCount = sizeof(kKeywordReplacements) / sizeof(kKeywordReplacements[0]);

String keyword_replacement(const String &p_name) {
    for (int i = 0; i < kKeywordReplacementCount; ++i) {
        if (p_name == kKeywordReplacements[i][0]) {
            return kKeywordReplacements[i][1];
        }
    }
    return String();
}

String remapped_primitive_type_name(Variant::Type p_type) {
    switch (p_type) {
        case Variant::NIL:
            return "any";
        case Variant::BOOL:
            return "boolean";
        case Variant::INT:
            return "int64";
        case Variant::FLOAT:
            return "float64";
        case Variant::STRING:
            return "string";
        default:
            return String();
    }
}

// `needs_quotes_regex = /^(?![$_A-Za-z])|[^\w$]/` implemented manually.
static bool s_name_needs_quotes(const String &p_name) {
    if (p_name.is_empty()) {
        return true;
    }
    const char32_t first = p_name[0];
    const bool first_valid = (first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z') || first == '_' || first == '$';
    if (!first_valid) {
        return true;
    }
    for (int i = 1; i < p_name.length(); ++i) {
        const char32_t c = p_name[i];
        const bool valid = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '$';
        if (!valid) {
            return true;
        }
    }
    return false;
}

static void s_append_escaped(String &r_out, const String &p_value) {
    for (int i = 0; i < p_value.length(); ++i) {
        switch (p_value[i]) {
            case '"':
                r_out += "\\\"";
                break;
            case '\\':
                r_out += "\\\\";
                break;
            case '\n':
                r_out += "\\n";
                break;
            case '\r':
                r_out += "\\r";
                break;
            case '\t':
                r_out += "\\t";
                break;
            default:
                r_out += p_value[i];
                break;
        }
    }
}

String js_number_to_string(double p_value) {
    // JavaScript Number.prototype.toString(): the SHORTEST decimal that
    // round-trips to the same double, rendered in fixed notation below 1e21.
    // (The old pipeline marshalled global constants through v8 Numbers, so
    // INT64_MAX/MIN print as -9223372036854776000, not the exact int64.)
    if (p_value == (double)(int64_t)p_value && fabs(p_value) < 9.0e15) {
        return String::num_int64((int64_t)p_value);
    }
    char buf[64];
    int precision = 17;
    for (int try_precision = 1; try_precision <= 17; ++try_precision) {
        snprintf(buf, sizeof(buf), "%.*e", try_precision, p_value);
        if (strtod(buf, nullptr) == p_value) {
            precision = try_precision;
            break;
        }
    }
    snprintf(buf, sizeof(buf), "%.*e", precision, p_value);

    // parse [sign] d.dddde[+-]XX
    const char *read = buf;
    bool negative = false;
    if (*read == '-' || *read == '+') {
        negative = (*read == '-');
        ++read;
    }
    char digits[32];
    int ndigits = 0;
    while (*read && *read != 'e' && *read != 'E') {
        if (*read >= '0' && *read <= '9') {
            digits[ndigits++] = *read;
        }
        ++read;
    }
    const int exponent = strtol(read + 1, nullptr, 10);
    digits[ndigits] = '\0';

    String out;
    if (negative) {
        out += "-";
    }
    if (exponent >= ndigits - 1 && exponent <= 20) {
        // fixed integer notation: mantissa + zero padding
        out += digits;
        for (int i = ndigits - 1; i < exponent; ++i) {
            out += "0";
        }
    } else {
        out += digits[0];
        out += ".";
        out += (digits + 1);
        out += "e" + String::num_int64(exponent);
    }
    return out;
}

String name_string(const String &p_name) {
    if (keyword_replacement(p_name).is_empty() && !s_name_needs_quotes(p_name)) {
        return p_name;
    }
    String quoted = "\"";
    s_append_escaped(quoted, p_name);
    quoted += "\"";
    return quoted;
}

String make_copyright_header(const String &p_filename) {
    String result;
    result += "/*******************************************************************************************************\n";
    result += " *  " + p_filename + "\n";
    result += " *******************************************************************************************************\n";
    result += " *  This file is Generated by:\n";
    result += " *        GodotJS-Ext (https://github.com/Daylily-Zeleen/GodotJS-Ext)\n";
    result += String::utf8(" *        - \xE5\xBF\x98\xE5\xBF\xA7\xE3\x81\xAE (Daylily-Zeleen) - Contact: daylily-zeleen@foxmail.com\n");
    result += " *******************************************************************************************************\n";
    result += " */\n";
    // TS: lines.join("\n") with a trailing "" element - the string ends with a
    // single newline after "*/" (no extra blank line).
    return result;
}

String join_type_name(const Vector<String> &p_parts) {
    String result;
    bool first = true;
    for (int i = 0; i < p_parts.size(); ++i) {
        if (p_parts[i].is_empty()) {
            continue;
        }
        if (!first) {
            result += " | ";
        }
        result += p_parts[i];
        first = false;
    }
    return result;
}

String get_js_array_type_name(const String &p_element_type_name) {
    if (p_element_type_name.is_empty()) {
        return String();
    }
    return p_element_type_name + String("[]");
}

} // namespace codegen
} // namespace jsb
