#pragma once
#include "jsb_bridge_pch.h"

namespace jsb {
class Environment;

struct JSValueMove {
	friend class Environment;

private:
	std::shared_ptr<Environment> env_;
	v8::Global<v8::Value> value_;

	JSValueMove() = default;
	JSValueMove(const std::shared_ptr<Environment> &p_env, const v8::Local<v8::Value> &p_value);

public:
	// disable copy to avoid unpredictable behaviours (for now)
	JSValueMove(const JSValueMove &p_other) = delete;
	JSValueMove &operator=(const JSValueMove &p_other) = delete;

	JSValueMove(JSValueMove &&p_other) noexcept = default;
	JSValueMove &operator=(JSValueMove &&p_other) noexcept = default;

	_FORCE_INLINE_ void ignore() const {}
	bool is_valid() const;
	String to_string() const;
	Variant to_variant() const;
	// Vector<String> to_strings() const;
};
} //namespace jsb
