#ifndef JSB_FLAGS_H
#define JSB_FLAGS_H

#include <godot_cpp/core/defs.hpp>

namespace templates {

template <typename T>
class BitField {
	using UnderlyingType = std::underlying_type<T>::type;
	UnderlyingType value = 0;

public:
	_FORCE_INLINE_ void set_flag(T p_flag) { value |= p_flag; }
	_FORCE_INLINE_ bool has_flag(T p_flag) const { return value & p_flag; }
	_FORCE_INLINE_ void clear_flag(T p_flag) { value &= ~p_flag; }
	_FORCE_INLINE_ BitField(T p_value) { value = p_value; }
	_FORCE_INLINE_ BitField() = default;
	_FORCE_INLINE_ operator UnderlyingType() const { return value; }
};

}

#endif // JSB_FLAGS_H