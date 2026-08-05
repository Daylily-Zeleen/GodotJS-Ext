#pragma once

#include "jsb_bridge_pch.h"

namespace jsb {
/**
 * This struct is *not* POD, but aims to be compatible with SArray's memory relocation logic.
 */
struct JavaScriptTimerAction {
	_FORCE_INLINE_ JavaScriptTimerAction() : function_(nullptr), argc_(0), argv_(nullptr) {
	}

	_FORCE_INLINE_ JavaScriptTimerAction(v8::Global<v8::Function> &&p_func, int p_argc) : argc_(p_argc) {
		function_ = new v8::Global<v8::Function>(std::move(p_func));

		if (p_argc > 0) {
			argv_ = new v8::Global<v8::Value>[p_argc];
		} else {
			argv_ = nullptr;
		}
	}

	_FORCE_INLINE_ ~JavaScriptTimerAction() {
		delete function_;
		delete[] argv_;
		function_ = nullptr;
		argv_ = nullptr;
	}

	JavaScriptTimerAction(JavaScriptTimerAction &p_other) = delete;

	_FORCE_INLINE_ JavaScriptTimerAction(JavaScriptTimerAction &&p_other) noexcept
			: function_(p_other.function_), argc_(p_other.argc_), argv_(p_other.argv_) {
		p_other.function_ = nullptr;
		p_other.argc_ = 0;
		p_other.argv_ = nullptr;
	}

	JavaScriptTimerAction &operator=(JavaScriptTimerAction &p_other) = delete;

	_FORCE_INLINE_ JavaScriptTimerAction &operator=(JavaScriptTimerAction &&p_other) noexcept {
		if (this != &p_other) {
			delete function_;
			delete[] argv_;

			function_ = p_other.function_;
			argc_ = p_other.argc_;
			argv_ = p_other.argv_;

			p_other.function_ = nullptr;
			p_other.argc_ = 0;
			p_other.argv_ = nullptr;
		}
		return *this;
	}

	_FORCE_INLINE_ explicit operator bool() const { return function_ && !function_->IsEmpty(); }

	_FORCE_INLINE_ void store(int index, v8::Global<v8::Value> &&value) {
		jsb_check(index >= 0 && index < argc_);
		argv_[index] = std::move(value);
	}

	void operator()(v8::Isolate *isolate);

private:
	v8::Global<v8::Function> *function_;
	int argc_;
	v8::Global<v8::Value> *argv_;
};
} //namespace jsb
