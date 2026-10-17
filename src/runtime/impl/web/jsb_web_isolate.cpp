/************************************************************************/
/*  jsb_web_isolate.cpp                                                 */
/************************************************************************/
/*  This file is part of:                                               */
/*                                GodotJS-Ext                           */
/*              https://github.com/Daylily-Zeleen/GodotJS-Ext           */
/*                                                                      */
/*  Copyright (c) 2026-present 忘忧の (Daylily-Zeleen)                  */
/*                 - Contact: daylily-zeleen@foxmail.com                */
/*  Copyright (c) Contributors of GodotJS                               */
/*                 - <https://github.com/godotjs/GodotJS>               */
/*                                                                      */
/*  This library is free software; you can redistribute it and/or       */
/*  modify it under the terms of the GNU Lesser General Public          */
/*  License as published by the Free Software Foundation; either        */
/*  version 2.1 of the License, or (at your option) any later version.  */
/*                                                                      */
/*  This library is distributed in the hope that it will be useful,     */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of      */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU    */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#include "jsb_web_isolate.h"

#include "jsb_web_catch.h"
#include "jsb_web_context.h"
#include "jsb_web_handle.h"

namespace v8 {
Isolate *Isolate::New(const CreateParams &params) {
	Isolate *isolate = memnew(Isolate);
	return isolate;
}

#ifdef __EMSCRIPTEN__
namespace {
// 跨线程释放句柄槽的载荷：堆分配（wasm 线性内存全线程共享，可安全跨线程传递）。
struct ReleaseHandlePayload {
	v8::Isolate *isolate;
	jsb::impl::HandleID handle;
	bool weak;
};
} //namespace
#endif

Isolate::Isolate() : ref_count_(1), disposed_(false), handle_scope_(nullptr) {
	// 不变式：同线程上 GlobalInitialize::init()（主线程 = 脚本语言构造，worker = _run 入口）
	// 必先于任何 Isolate 创建，glue 在那一刻已按线程建立，这里不再重复 ensure。
#ifdef __EMSCRIPTEN__
	// 记录属主线程：每个 pthread 一个 JS realm，realm 里的句柄槽只能被该线程访问/释放。
	owner_thread_ = pthread_self();
	release_queue_ = em_proxying_queue_create();
#endif
	rt_ = jsbi_NewEngine(this);
}

bool Isolate::owns_current_thread() const {
#ifdef __EMSCRIPTEN__
	return owner_thread_ != 0 && pthread_equal(owner_thread_, pthread_self()) != 0;
#else
	return true;
#endif
}

void Isolate::proxy_release_handle(jsb::impl::HandleID p_handle, bool p_weak) {
#ifdef __EMSCRIPTEN__
	if (release_queue_ == nullptr) {
		return;
	}
	// NOTE 不能写成 `memnew(T{ a, b })`：memnew 是函数式宏，逗号会被当成多参数。
	ReleaseHandlePayload *payload = memnew(ReleaseHandlePayload);
	payload->isolate = this;
	payload->handle = p_handle;
	payload->weak = p_weak;
	// 属主线程会在自己的 realm 里执行 thunk；若该线程已退出（runtime 不在了）则投递失败——
	// 此时 realm 已随线程消失，直接丢弃。
	if (!emscripten_proxy_async(release_queue_, owner_thread_, &Isolate::_release_handle_thunk, payload)) {
		memdelete(payload);
	}
#endif
}

/*static*/ void Isolate::_release_handle_thunk(void *p_arg) {
#ifdef __EMSCRIPTEN__
	ReleaseHandlePayload *payload = static_cast<ReleaseHandlePayload *>(p_arg);
	JSB_WEB_LOG(VeryVerbose, "released cross-thread handle %d on its owner thread", payload->handle);
	if (payload->weak) {
		jsb::impl::Broker::SetWeakCallback(payload->isolate, payload->handle, nullptr, nullptr);
	}
	jsbi_handle_Reset(jsb::impl::Broker::get_engine(payload->isolate), payload->handle);
	memdelete(payload);
#endif
}

Isolate::~Isolate() {
	jsb_check(!rt_);
}

void Isolate::_release() {
	JSB_WEB_LOG(VeryVerbose, "release web runtime");

	// cleanup
	jsb_check(!handle_scope_);

#ifdef __EMSCRIPTEN__
	// 丢弃尚未消费的跨线程释放请求（realm 即将拆除，无人能再安全访问它的 JS 堆）
	if (release_queue_ != nullptr) {
		em_proxying_queue_destroy(release_queue_);
		release_queue_ = nullptr;
	}
#endif

	// make it behave like v8, not to trigger gc callback after the isolate disposed
	internal_data_.clear();

	// dispose the runtime
	jsbi_FreeEngine(rt_);
	rt_ = {};

	memdelete(this);
}

void Isolate::Dispose() {
	jsb_check(!disposed_);
	disposed_ = true;
	_remove_reference();
}

void Isolate::SetData(int index, void *data) {
	jsb_check(index == 0);
	embedder_data_ = data;
}

Local<Context> Isolate::GetCurrentContext() {
	return Local<Context>(Data(this, 0));
}

} //namespace v8
