#ifndef GODOTJS_QUICKJS_CATCH_H
#define GODOTJS_QUICKJS_CATCH_H

#include <godot_cpp/variant/string.hpp>

namespace v8 {
class Isolate;
}

namespace jsb::impl {
class TryCatch {
public:
	v8::Isolate *isolate_;

	TryCatch(v8::Isolate *isolate) : isolate_(isolate) {}
	~TryCatch() = default;

	v8::Isolate *get_isolate() const { return isolate_; }

	bool has_caught() const;
	void get_message(godot::String *r_message, godot::String *r_stacktrace = nullptr) const;
};
} //namespace jsb::impl
#endif
