#pragma once

#include <godot_cpp/classes/os.hpp>

namespace godot::ThreadEx {
    using ID = uint64_t;
    constexpr ID UNASSIGNED_ID = 0;   
    static _FORCE_INLINE_ ID get_caller_id() { return ::godot::OS::get_singleton()->get_thread_caller_id(); }
} // namespace Thread
