#pragma once

#include "../jsb.config.h"

#include <cstdint>

namespace godot {

#ifndef TTR
#	define TTR(text) (text)
#endif // TTR

using ObjectInstanceID = uint64_t; // NOTE: 与 Object::get_instance_id() 返回值类型一致

#ifndef SNAME
#	define SNAME(text) [] {static StringName sn {text}; return sn; }()
#endif // SNAME

} //namespace godot
