#include "jsb_variant_util.h"
#include <godot_cpp/variant/array.hpp>


namespace jsb::internal
{
    Variant VariantUtil::structured_clone(const Variant& p_variant, ReferentialVariantMap<Variant>& p_clone_map, bool& r_valid, int p_recursion_count)
    {
        if (p_recursion_count == 0)
        {
            r_valid = true;
        }

        Variant* existing_clone = p_clone_map.getptr(p_variant);

        if (existing_clone)
        {
            return *existing_clone;
        }

        Variant clone;

        switch (p_variant.get_type())
        {
            case Variant::Type::OBJECT:
                ERR_PRINT("Structured clone cannot clone Godot Objects. Godot Objects must be transferred");
                r_valid = false;
                clone = Variant();
                break;
            case Variant::Type::DICTIONARY:
            {
                Dictionary original = p_variant;
                Dictionary dict_clone;
                dict_clone.set_typed(original.get_typed_key_builtin(), original.get_typed_key_class_name(), original.get_typed_key_script(),
                        original.get_typed_value_builtin(), original.get_typed_value_class_name(), original.get_typed_value_script());

                if (p_recursion_count > MAX_RECURSION)
                {
                    ERR_PRINT("Max recursion reached");
                    r_valid = false;
                    return dict_clone;
                }

                p_recursion_count++;

                // GDExtension: use Dictionary::keys() instead of get_key_list()
                {
                    Array keys = original.keys();
                    for (int idx = 0; idx < keys.size(); idx++)
                    {
                        const Variant& key = keys[idx];
                        dict_clone[structured_clone(key, p_clone_map, r_valid, p_recursion_count)] =
                            structured_clone(original[key], p_clone_map, r_valid, p_recursion_count);
                    }
                }

                clone = dict_clone;
                break;
            }
            case Variant::Type::ARRAY:
            {
                Array original = p_variant;
                Array arr_clone;
                arr_clone.set_typed(original.get_typed_builtin(), original.get_typed_class_name(), original.get_typed_script());

                if (p_recursion_count > MAX_RECURSION)
                {
                    ERR_PRINT("Max recursion reached");
                    r_valid = false;
                    return arr_clone;
                }

                p_recursion_count++;

                int element_count = original.size();
                arr_clone.resize(element_count);

                for (int i = 0; i < element_count; i++)
                {
                    arr_clone.set(i, structured_clone(original.get(i), p_clone_map, r_valid, p_recursion_count));
                }

                clone = arr_clone;
                break;
            }
            case Variant::Type::PACKED_BYTE_ARRAY:
                clone = p_variant.operator PackedByteArray().duplicate();
                break;
            case Variant::Type::PACKED_INT32_ARRAY:
                clone = p_variant.operator PackedInt32Array().duplicate();
                break;
            case Variant::Type::PACKED_INT64_ARRAY:
                clone = p_variant.operator PackedInt64Array().duplicate();
                break;
            case Variant::Type::PACKED_FLOAT32_ARRAY:
                clone = p_variant.operator PackedFloat32Array().duplicate();
                break;
            case Variant::Type::PACKED_FLOAT64_ARRAY:
                clone = p_variant.operator PackedFloat64Array().duplicate();
                break;
            case Variant::Type::PACKED_STRING_ARRAY:
                clone = p_variant.operator PackedStringArray().duplicate();
                break;
            case Variant::Type::PACKED_VECTOR2_ARRAY:
                clone = p_variant.operator PackedVector2Array().duplicate();
                break;
            case Variant::Type::PACKED_VECTOR3_ARRAY:
                clone = p_variant.operator PackedVector3Array().duplicate();
                break;
            case Variant::Type::PACKED_COLOR_ARRAY:
                clone = p_variant.operator PackedColorArray().duplicate();
                break;
            case Variant::Type::PACKED_VECTOR4_ARRAY:
                clone = p_variant.operator PackedVector4Array().duplicate();
                break;
            default:
                clone = p_variant;
        }

        p_clone_map[p_variant] = clone;
        return clone;
    }
}
