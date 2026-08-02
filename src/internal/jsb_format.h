#ifndef GODOTJS_FORMAT_H
#define GODOTJS_FORMAT_H

#include "../compat/jsb_compat.h"
#include "jsb_sindex.h"

#include <godot_cpp/variant/variant.hpp>

namespace jsb::internal
{
    template<typename T> struct TFormat                { static Variant from(const T& p_item) { return p_item; } };
    template<>           struct TFormat<IndexSafe64>   { static Variant from(const IndexSafe64& p_item) { return *p_item; } };
    template<>           struct TFormat<Index64>       { static Variant from(const Index64& p_item) { return *p_item; } };
    template<>           struct TFormat<Index32>       { static Variant from(const Index32& p_item) { return *p_item; } };
    template<>           struct TFormat<uintptr_t>     { static Variant from(const uintptr_t& p_item) { return Variant((uint64_t) p_item); } };
    template<typename T> static Variant convert(const T& p_item) { return TFormat<T>::from(p_item); }

    template <typename... VarArgs>
    String format(const String &p_text, const VarArgs... p_args)
    {
        return vformat(p_text, convert(p_args)...);
    }
}
#endif

