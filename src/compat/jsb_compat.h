#ifndef GODOTJS_COMPAT_H
#define GODOTJS_COMPAT_H

#include "godot_cpp/core/method_ptrcall.hpp"
#include "jsb_engine_version_comparison.h"
#include "jsb_paged_allocator.h"
#include "jsb_ring_buffer.h"
#include "jsb_rw_lock.h"

namespace godot {
using ObjectInstanceID = decltype(Object().get_instance_id());
} //namespace godot

#define TTR(text) (text)
#define SNAME(text) [] {static StringName sn {text}; return sn; }()

static void object_get_instance_binding(Object *p_obj, void *p_token, const GDExtensionInstanceBindingCallbacks *p_callbacks) {
	void *obj_ptr {nullptr};
	PtrToArg<Object*>::encode(p_obj, &obj_ptr);
	::godot::gdextension_interface::object_get_instance_binding(obj_ptr, p_token, p_callbacks);
}

template <typename StrArray>
static godot::String string_join(const godot::String &separator, const StrArray &parts) {
	if (parts.is_empty())
		return {};
	else if (parts.size() == 1)
		return parts[0];

	const int this_length = separator.length();

	int new_size = (parts.size() - 1) * this_length;
	for (const String &part : parts) {
		new_size += part.length();
	}
	new_size += 1;

	String ret;
	ret.resize(new_size);
	char32_t *ret_ptrw = ret.ptrw();
	const char32_t *this_ptr = separator.ptr();

	bool first = true;
	for (const String &part : parts) {
		if (first) {
			first = false;
		} else if (this_length) {
			memcpy(ret_ptrw, this_ptr, this_length * sizeof(char32_t));
			ret_ptrw += this_length;
		}

		const int part_length = part.length();
		if (part_length) {
			memcpy(ret_ptrw, part.ptr(), part_length * sizeof(char32_t));
			ret_ptrw += part_length;
		}
	}

	*ret_ptrw = 0;

	return ret;
}

#endif
