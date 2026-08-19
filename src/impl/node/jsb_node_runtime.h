/************************************************************************/
/*  jsb_node_runtime.h                                                  */
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

#pragma once

#include "jsb_node_pch.h"

namespace jsb::impl {

class NodeRuntime {
public:
	NodeRuntime();
	~NodeRuntime();

	// pump the per-isolate uv loop + v8 microtasks (from `Environment::update()`)
	void PumpEventLoop();

	// fallback of `Builtins::_require` in node mode: require a node module.
	// returns an empty handle if the module cannot be resolved (caller should
	// fall back to the GodotJS module loader).
	v8::Global<v8::Value> NodeRequire(const v8::Local<v8::String> &p_module_id) const;

	_FORCE_INLINE_ v8::Isolate *get_isolate() const { return isolate_; }
	_FORCE_INLINE_ v8::Local<v8::Context> get_node_context() const { return node_context_.Get(isolate_); }

private:
	// generate the bootstrap script (a small CommonJS-style glue that wires
	// 'godot' module, res:// aware fs and process.dlopen, and console redirect)
	std::string bootstrap_script() const;

	// `struct uv_loop_s` is forward-declared by node.h; the full libuv API is only
	// needed in jsb_node_runtime.cpp (which includes <uv.h> itself).
	struct uv_loop_s *loop_ = nullptr;
	v8::Isolate *isolate_ = nullptr;
	// Held for the whole lifetime of the isolate on the creating thread. The
	// node MultiIsolatePlatform requires a v8::Locker for *every* V8 entry, and
	// not all jsb call paths (e.g. the worker thread's load/module pipeline)
	// go through JSB_ISOLATE_SCOPE. Locker is nestable, so the per-call
	// JSB_ISOLATE_SCOPE lockers remain fine.
	std::unique_ptr<v8::Locker> locker_;
	std::unique_ptr<node::ArrayBufferAllocator> allocator_;
	node::IsolateData *isolate_data_ = nullptr;
	node::Environment *node_env_ = nullptr;
	v8::Global<v8::Context> node_context_;
};

} //namespace jsb::impl
