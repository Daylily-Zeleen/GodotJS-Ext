/************************************************************************/
/*  jsb_node_runtime.cpp                                                */
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

#include "jsb_node_runtime.h"

// libuv API used by this TU only (kept out of the pch to avoid the windows.h
// SEVERITY_* macro pollution, see jsb_node_pch.h).
#include <uv.h>

#include "jsb_node_bridge.h"
#include "jsb_node_global_init.h"
#include "jsb_node_helper.h"

namespace jsb::impl {
NodeRuntime::NodeRuntime() {
	allocator_ = node::ArrayBufferAllocator::Create();
	jsb_ensure(allocator_);

	loop_ = memnew(uv_loop_t);
	jsb_ensure(loop_);
	const int err = uv_loop_init(loop_);
	jsb_ensuref(err == 0, "uv_loop_init failed: %d", uv_err_name(err));

	node::MultiIsolatePlatform *platform = GlobalInitialize::get_platform();
	jsb_ensure(platform);

	/** Step 1: Create isolate  */
	{
		// node::NewIsolate internally registers the isolate on the platform
		// and sets the node-specific isolate settings.
		isolate_ = node::NewIsolate(allocator_.get(), loop_, platform);
		jsb_ensure(isolate_);
		// Lock the isolate for the remainder of its lifetime. The MultiIsolatePlatform
		// asserts on any V8 entry without a Locker, and jsb has call paths (notably
		// the worker thread) that enter V8 without going through JSB_ISOLATE_SCOPE.
		// The creating thread exclusively owns this isolate, so holding the Locker
		// forever is correct; nested lockers in JSB_ISOLATE_SCOPE are a no-op.
		locker_ = std::make_unique<v8::Locker>(isolate_);
	}

	/** Isolate Data, Context, node Environment */
	{
		/** Step 2: Create Isolate data. */
		JSB_ISOLATE_SCOPE(isolate_);
		v8::HandleScope handle_scope(isolate_);

		isolate_data_ = node::CreateIsolateData(isolate_, loop_, platform, allocator_.get());
		jsb_ensure(isolate_data_);

		/** Step 3: Create Context */
		const v8::Local<v8::Context> context = node::NewContext(isolate_);
		jsb_ensure(!context.IsEmpty());
		node_context_.Reset(isolate_, context);
		v8::Context::Scope context_scope(context);

		/** Step 4: Create Environment. */
		const std::vector<std::string> args = { "godotjs-ext", "--experimental-vm-modules" };
		const std::vector<std::string> exec_args;
		// NOTE: intentionally NOT using node::EnvironmentFlags::kDefaultFlags.
		// env.cc expands kDefaultFlags by ORing in kOwnsProcessState | kOwnsInspector,
		// and every Environment that owns an inspector triggers a *process-wide*
		// inspector agent startup (inspector_agent.cc CHECK_EQ on a process-static
		// flag). jsb creates multiple node::Environment instances (the main env plus
		// the shadow/parser env), so the second one would abort the process. jsb has
		// its own debugger bridge (jsb_debugger.cpp), the node inspector is unused.
		// kOwnsProcessState keeps process-level behaviour (cwd, title, ...) intact.
		node_env_ = node::CreateEnvironment(isolate_data_, context, args, exec_args, node::EnvironmentFlags::kOwnsProcessState);
		jsb_ensure(node_env_);
	}

	/** Step 5: Initialize the environment with bootstrap script.*/
	{
		JSB_ISOLATE_SCOPE(isolate_);
		v8::HandleScope handle_scope(isolate_);
		const v8::Local<v8::Context> context = node_context_.Get(isolate_);
		v8::Context::Scope context_scope(context);

		// register the 'godot' linked binding (accessed via process._linkedBinding('godot'))
		NodeBridge::AddGodotLinkedBinding(node_env_);

		// run the bootstrap script (wires 'godot' module, res:// aware fs, process.dlopen)
		const std::string script = bootstrap_script();
		const v8::MaybeLocal<v8::Value> bootstrap_result = node::LoadEnvironment(node_env_, script.c_str());
		jsb_unused(bootstrap_result);
	}

	// drive the event loop once so the bootstrap finishes (installs timers/console etc.)
	isolate_->PerformMicrotaskCheckpoint();
	uv_run(loop_, UV_RUN_ONCE);
}

NodeRuntime::~NodeRuntime() {
	// TODO: 找不到node 构建在退出进程时的 89 个 Orphan StringName 怎么处理，orz。
	// Node Environment.
	{
		jsb_check(node_env_);
		JSB_ISOLATE_SCOPE(isolate_);
		v8::HandleScope handle_scope(isolate_);
		const v8::Local<v8::Context> context = node_context_.Get(isolate_);
		v8::Context::Scope context_scope(context);

		isolate_->LowMemoryNotification();
		node::SpinEventLoop(node_env_).ToChecked(); // 如果有未完成任务（如 setInterval） 可能会卡住
	}
	node::Stop(node_env_);
	node::FreeEnvironment(node_env_);

	{
		v8::Isolate::Scope isolate_scope(isolate_);
		node::FreeIsolateData(isolate_data_);
	}

	// Context
	node_context_.Reset();

	// Isolate
	{
		jsb_check(locker_);
		jsb_check(isolate_);
		// the Locker must be released before the isolate is disposed.
		locker_.reset();
		jsb_ensure(GlobalInitialize::get_platform());
		bool platform_finished = false;
		GlobalInitialize::get_platform()->AddIsolateFinishedCallback(isolate_, [](void *data) {
			*static_cast<bool *>(data) = true;
		}, &platform_finished);

		GlobalInitialize::get_platform()->DisposeIsolate(isolate_);
		while (!platform_finished) {
			uv_run(loop_, UV_RUN_ONCE);
		}
	}

	// uv loop
	{
		uv_stop(loop_);
		uv_walk(loop_, [](uv_handle_t *handle, void *arg) {
			if (!uv_is_closing(handle)) {
				uv_close(handle, nullptr);
			}
		}, nullptr);
		uv_run(loop_, UV_RUN_DEFAULT);
		jsb_check(uv_loop_alive(loop_) == 0);
		int err = uv_loop_close(loop_);
		if (err != 0) {
			WARN_PRINT("uv_loop_close: " + String(uv_strerror(err)));
		}
		memdelete(loop_);
	}

	// Allocator
	allocator_.reset();
}

void NodeRuntime::PumpEventLoop() {
	if (!node_env_ || node_context_.IsEmpty()) {
		return;
	}

	// node callbacks may come from the uv threadpool, take the locker like gode does.
	JSB_ISOLATE_SCOPE(isolate_);
	v8::HandleScope handle_scope(isolate_);

	const v8::Local<v8::Context> context = node_context_.Get(isolate_);
	v8::Context::Scope context_scope(context);

	isolate_->PerformMicrotaskCheckpoint();
	uv_run(loop_, UV_RUN_NOWAIT);
	jsb_ensure(GlobalInitialize::get_platform());
	GlobalInitialize::get_platform()->DrainTasks(isolate_);
	isolate_->PerformMicrotaskCheckpoint();
}

v8::Global<v8::Value> NodeRuntime::NodeRequire(const v8::Local<v8::String> &p_module_id) const {
	jsb_ensure(node_env_ && !node_context_.IsEmpty());

	JSB_ISOLATE_SCOPE(isolate_);
	v8::HandleScope handle_scope(isolate_);
	const v8::Local<v8::Context> context = node_context_.Get(isolate_);
	v8::Context::Scope context_scope(context);
	v8::TryCatch try_catch(isolate_);

	v8::Global<v8::Value> ret;
	// the bootstrap script installs a global helper that closes over node's require
	const v8::Local<v8::String> fn_name = v8::String::NewFromUtf8(isolate_, "__godotjs_node_require");
	v8::Local<v8::Value> fn_val;
	if (!context->Global()->Get(context, fn_name).ToLocal(&fn_val) || !fn_val->IsFunction()) {
		return ret;
	}

	v8::Local<v8::Value> argv[] = { p_module_id };
	v8::Local<v8::Value> result;
	if (!fn_val.As<v8::Function>()->Call(context, context->Global(), 1, argv).ToLocal(&result)) {
		if (try_catch.HasCaught()) {
			// 调用方可以根据返回结果知道是否成功，这里避免强行抛异常，只输出一条消息。
			JSB_LOG(Verbose, "node require('%s') failed: %s", Helper::to_string(isolate_, p_module_id), Helper::to_string_without_side_effect(isolate_, try_catch.Exception()));
		}
		return ret;
	}

	ret.Reset(isolate_, result);
	return ret;
}

std::string NodeRuntime::bootstrap_script() const {
	// NOTE: executed as a plain global script by node::LoadEnvironment,
	// `require` resolves builtin modules (fs/path/module/...) and
	// `process._linkedBinding('godot')` returns the 'godot' linked binding.
	return R"(
// ===== GodotJS-Ext node bootstrap =====
const Module = require('module');
const path = require('path');
const fs = require('fs');
const godotModule = process._linkedBinding('godot');

const isGodotPath = (p) => typeof p === 'string' && (p.startsWith('res://') || p.startsWith('user://'));
const toOsPath = (p) => isGodotPath(p) ? path.join(process.cwd(), p.slice(6)) : p;

// --- register the 'godot' module so user code can require('godot') ---
try {
  const m = new Module('godot');
  m.id = 'godot';
  m.exports = godotModule;
  m.loaded = true;
  Module._cache['godot'] = m;
} catch (e) {}

const originalProtoRequire = Module.prototype.require;
Module.prototype.require = function(id) {
  if (id === 'godot' || id === 'godot-jsb') return godotModule;
  return originalProtoRequire.call(this, id);
};

// the jsb bridge installs its own global require; make it redirect 'godot' too
const originalGlobalRequire = globalThis.require;
if (typeof originalGlobalRequire === 'function') {
  const patched = function(id) {
    if (id === 'godot' || id === 'godot-jsb') return godotModule;
    return originalGlobalRequire.call(this, id);
  };
  try { Object.assign(patched, originalGlobalRequire); } catch (e) {}
  globalThis.require = patched;
}

// helper used by Builtins::_require fallback (NodeRuntime::NodeRequire)
globalThis.__godotjs_node_require = function(id) { return require(id); };

// --- res:// aware fs ---
const wrapFs = (fn) => function(p, ...rest) { return fn.call(this, isGodotPath(p) ? toOsPath(p) : p, ...rest); };
const fsPatchNames = ['readFileSync','readFile','statSync','stat','existsSync','lstatSync','lstat','openSync','realpathSync','realpath','accessSync','access','mkdirSync','mkdir','readdirSync','readdir'];
for (const n of fsPatchNames) { if (typeof fs[n] === 'function') fs[n] = wrapFs(fs[n]); }

// res:// reads go through Godot's FileAccess (works inside .pck too)
const originalReadFileSync = fs.readFileSync;
fs.readFileSync = function(p, options) {
  if (typeof p === 'string' && p.startsWith('res://')) {
    const content = godotModule.fs_readFile(p);
    if (content !== null && content !== undefined) return content;
  }
  return originalReadFileSync.call(this, p, options);
};
const originalStatSync = fs.statSync;
fs.statSync = function(p, options) {
  if (typeof p === 'string' && p.startsWith('res://')) {
    const st = godotModule.fs_stat(p);
    if (st !== null && st !== undefined) return st;
  }
  return originalStatSync.call(this, p, options);
};

// --- process.dlopen for native .node addons ---
const _originalDlopen = process.dlopen;
process.dlopen = function(mod, filename, flags) {
  let realPath = filename;
  if (typeof filename === 'string') {
    let p = filename;
    if (p.startsWith('file://')) { try { p = require('url').fileURLToPath(p); } catch (e) {} }
    if (p.startsWith('res://')) { p = toOsPath(p); }
    if (p.startsWith('\\\\?\\')) { p = p.slice(4); } // strip the \\?\ prefix
    realPath = p;
  }
  // before loading a .node addon, preload its sibling DLLs on Windows
  if (typeof realPath === 'string' && realPath.endsWith('.node') && typeof godotModule.preload_dlls === 'function') {
    try { godotModule.preload_dlls(path.dirname(realPath)); } catch (e) {}
  }
  return arguments.length >= 3 ? _originalDlopen.call(this, mod, realPath, flags) : _originalDlopen.call(this, mod, realPath);
};

// --- child_process.fork: embedded node reports the host executable as
// process.execPath, so forking a probe would spawn Godot itself. Redirect
// fork() to the bundled godotjs-ext helper executable (which runs a real
// node process for native addon probing); keep a mock fallback for internal
// probes when no helper is installed ---
(function() {
  try {
    const cp = require('child_process');
    const { EventEmitter } = require('events');
    const _originalFork = cp.fork;
    if (typeof _originalFork !== 'function') return;
    let _cachedForkExecPath;
    const _isFile = (value) => {
      try { return typeof value === 'string' && fs.existsSync(value) && fs.statSync(value).isFile(); } catch (_) { return false; }
    };
    const _makeExecutable = (value) => {
      if (process.platform === 'win32') return;
      try { fs.chmodSync(value, 0o755); } catch (_) {}
    };
    const _normalizeForkModulePath = (value) => {
      if (typeof value !== 'string') return value;
      let p = value;
      if (p.startsWith('file://')) { try { p = require('url').fileURLToPath(p); } catch (_) {} }
      if (isGodotPath(p)) p = toOsPath(p);
      if (p.startsWith('\\\\?\\')) p = p.slice(4);
      return p;
    };
    const _bundledForkExecPath = () => {
      if (_cachedForkExecPath !== undefined) return _cachedForkExecPath;
      _cachedForkExecPath = null;
      const candidates = [];
      try { if (typeof godotModule.native_probe_executable === 'function') candidates.push(godotModule.native_probe_executable()); } catch (_) {}
      const platformDir = process.platform === 'win32' ? 'windows' : process.platform === 'darwin' ? 'macos' : process.platform;
      const archDir = process.arch === 'x64' ? 'x64' : process.arch === 'arm64' ? 'arm64' : process.arch;
      const exeName = process.platform === 'win32' ? 'godotjs-ext.exe' : 'godotjs-ext';
      candidates.push(path.join(path.dirname(process.execPath), 'addons', 'godotjs-ext.daylily-zeleen', 'bin', platformDir, archDir, exeName));
      for (const candidate of candidates) {
        if (typeof candidate !== 'string' || candidate.length === 0) continue;
        const normalized = _normalizeForkModulePath(candidate);
        if (_isFile(normalized)) { _makeExecutable(normalized); _cachedForkExecPath = normalized; break; }
      }
      return _cachedForkExecPath;
    };
    const _isNativeAddonProbe = (modulePath) =>
      typeof modulePath === 'string' && path.basename(modulePath).startsWith('testBindingBinary');
    const _fallbackProbe = () => {
      const mock = new EventEmitter();
      mock.pid = 0; mock.exitCode = null; mock.killed = false;
      mock.stdout = null; mock.stderr = null;
      mock.kill = function() { this.killed = true; if (this.exitCode === null) this.exitCode = 1; };
      mock.send = function(msg) {
        const self = this;
        if (msg && msg.type === 'start') {
          process.nextTick(() => self.emit('message', { type: 'loaded' }));
        } else if (msg && msg.type === 'test') {
          process.nextTick(() => {
            if (msg.gpu === false) self.emit('message', { type: 'done' });
            else { mock.exitCode = 1; self.emit('exit', 1, null); }
          });
        } else if (msg && msg.type === 'exit') {
          process.nextTick(() => { mock.exitCode = 0; self.emit('exit', 0, null); });
        }
        return true;
      };
      process.nextTick(() => mock.emit('message', { type: 'ready' }));
      return mock;
    };
    cp.fork = function(modulePath, args, options) {
      const forkArgs = Array.isArray(args) ? args : [];
      const optionSource = Array.isArray(args) ? options : (args === undefined ? options : args);
      const forkOptions = { ...(optionSource || {}) };
      const execPath = _bundledForkExecPath();
      if (typeof execPath === 'string') {
        const normalizedModulePath = _normalizeForkModulePath(modulePath);
        if (forkOptions.execPath === undefined) forkOptions.execPath = execPath;
        return _originalFork.call(this, normalizedModulePath, forkArgs, forkOptions);
      }
      if (_isNativeAddonProbe(modulePath)) {
        return _fallbackProbe();
      }
      throw new Error('[godotjs-ext] child_process.fork requires the bundled godotjs-ext helper executable, but it was not found next to the godotjs-ext module. Rebuild with use_node=yes (which produces bin/<platform>/godotjs-ext) or reinstall the addon.');
    };
  } catch (e) {}
})();
)";
}

} //namespace jsb::impl
