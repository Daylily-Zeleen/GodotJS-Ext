#include "jsb_node_bridge.h"

#include "jsb_node_helper.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>

#ifdef WINDOWS_ENABLED
#	include <windows.h>
#elif defined(LINUX_ENABLED) || defined(MACOS_ENABLED) || defined(ANDROID_ENABLED)
#	include <dlfcn.h>
#	include <sys/stat.h>
#	include <unistd.h>
#endif

/**
 * TODO: 参考 Gode
 *  3. 通过ExportPlugin确保额外的二进制文件被打包
 *  4. 添加 node 相关的测试
 *  5. 添加 CI 验证文件货配置完整性
 */

namespace jsb::impl {
namespace {
// [windows] get the absolute path of the module that owns the given handle.
#ifdef WINDOWS_ENABLED
String get_module_file_name(HMODULE module) {
	CharWideString path;
	path.resize_uninitialized(MAX_PATH);
	for (;;) {
		const DWORD length = GetModuleFileNameW(module, path.ptrw(), static_cast<DWORD>(path.size()));
		if (length == 0) {
			return {};
		}
		if (length < static_cast<DWORD>(path.size())) {
			return String(path.get_data());
		}
		path.resize_uninitialized(path.size() * 2);
	}
}
#elif defined(LINUX_ENABLED) || defined(MACOS_ENABLED) || defined(ANDROID_ENABLED)
// [posix] get the absolute path of the module that owns the given symbol.
String get_module_file_name(void *symbol) {
	Dl_info info = {};
	if (dladdr(symbol, &info) == 0 || !info.dli_fname) {
		return {};
	}
	return String::utf8(info.dli_fname);
}

// [posix] true when the path points to an existing regular file.
bool native_file_exists(const String &path) {
	struct stat st = {};
	return stat(path.utf8().get_data(), &st) == 0 && S_ISREG(st.st_mode);
}

// [posix] ensure the file has the executable bits set.
void make_native_executable(const String &path) {
	struct stat st = {};
	const CharString path_utf8 = path.utf8();
	if (stat(path_utf8.get_data(), &st) == 0) {
		chmod(path_utf8.get_data(), st.st_mode | S_IXUSR | S_IXGRP | S_IXOTH);
	}
}

// libnode is statically linked into this module, so its N-API symbols are not
// globally visible by default. re-open the current module with RTLD_GLOBAL so
// that dynamically loaded .node addons can resolve napi_*.
void promote_current_module_symbols(void *symbol) {
	Dl_info info = {};
	if (dladdr(symbol, &info) == 0 || !info.dli_fname) {
		return;
	}
#	ifdef RTLD_NOLOAD
	if (void *existing_handle = dlopen(info.dli_fname, RTLD_NOW | RTLD_GLOBAL | RTLD_NOLOAD)) {
		return;
	}
#	endif
	(void)dlopen(info.dli_fname, RTLD_NOW | RTLD_GLOBAL);
}
#endif

// [v8 api] read a whole file through Godot's FileAccess.
// supports res:// paths and .pck packages. returns a Uint8Array, or null when
// the file does not exist.
void fs_read_file(const v8::FunctionCallbackInfo<v8::Value> &info) {
	v8::Isolate *isolate = info.GetIsolate();
	v8::Local<v8::Context> context = isolate->GetCurrentContext();

	if (info.Length() < 1 || !info[0]->IsString()) {
		jsb_throw(isolate, "bad argument");
		return;
	}
	const String path = Helper::to_string(isolate, info[0]);
	const Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
	if (file.is_null()) {
		info.GetReturnValue().SetNull();
		return;
	}

	const uint64_t length = file->get_length();
	const v8::Local<v8::ArrayBuffer> buffer = v8::ArrayBuffer::New(isolate, length);
	if (length > 0) {
		if (std::shared_ptr<v8::BackingStore> store = buffer->GetBackingStore()) {
			file->get_buffer((uint8_t *)store->Data(), length);
		}
	}
	info.GetReturnValue().Set(v8::Uint8Array::New(buffer, 0, length));
}

// [v8 api] stat a path through Godot's resource system.
// returns { isFile, isDirectory, size } or null when the path does not exist.
void fs_stat(const v8::FunctionCallbackInfo<v8::Value> &info) {
	v8::Isolate *isolate = info.GetIsolate();
	v8::Local<v8::Context> context = isolate->GetCurrentContext();

	if (info.Length() < 1 || !info[0]->IsString()) {
		jsb_throw(isolate, "bad argument");
		return;
	}
	const String path = Helper::to_string(isolate, info[0]);

	const bool is_dir = DirAccess::dir_exists_absolute(path);
	const bool is_file = FileAccess::file_exists(path);
	if (!is_dir && !is_file) {
		info.GetReturnValue().SetNull();
		return;
	}

	const v8::Local<v8::Object> stat = v8::Object::New(isolate);
	stat->Set(context, Helper::new_string_ascii(isolate, "isFile"), v8::Boolean::New(isolate, is_file)).Check();
	stat->Set(context, Helper::new_string_ascii(isolate, "isDirectory"), v8::Boolean::New(isolate, is_dir)).Check();
	if (is_file) {
		const Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
		if (!file.is_null()) {
			stat->Set(context, Helper::new_string_ascii(isolate, "size"), v8::Number::New(isolate, (double)file->get_length())).Check();
		}
	}
	info.GetReturnValue().Set(stat);
}

// [v8 api] preload all DLLs under a directory (Windows only).
// Native .node addons often depend on sibling DLLs (e.g. ggml-cuda.dll);
// we append the directory to PATH and force-load every DLL so that the
// addon's implicit dependencies resolve.
void preload_dlls(const v8::FunctionCallbackInfo<v8::Value> &info) {
#ifdef WINDOWS_ENABLED
	v8::Isolate *isolate = info.GetIsolate();
	if (info.Length() < 1 || !info[0]->IsString()) {
		return;
	}
	const String dir = Helper::to_string(isolate, info[0]);

	// append the directory to PATH
	{
		char path_buf[32768];
		const DWORD path_len = GetEnvironmentVariableA("PATH", path_buf, sizeof(path_buf));
		if (path_len > 0 && path_len < sizeof(path_buf)) {
			const String new_path = dir + String(";") + path_buf;
			SetEnvironmentVariableA("PATH", new_path.utf8().get_data());
		}
	}

	// preload every dll in the directory
	SetDllDirectoryW(dir.wide_string().get_data());

	const String pattern = dir.path_join("*.dll");
	WIN32_FIND_DATAW fd;
	const HANDLE h = FindFirstFileW(pattern.wide_string().get_data(), &fd);
	if (h != INVALID_HANDLE_VALUE) {
		do {
			const String dll_path = dir.path_join(String(fd.cFileName));
			LoadLibraryExW(dll_path.wide_string().get_data(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
		} while (FindNextFileW(h, &fd));
		FindClose(h);
	}
#endif
}

// [internal] return the absolute path of the standalone node helper executable
// used as the execPath for child_process.fork() probes (godotjs-ext.exe /
// godotjs-ext), or an empty string when the platform ships no helper
// (android/ios).
String get_native_probe_executable_path() {
#ifdef WINDOWS_ENABLED
	// the helper is shipped next to this module (bin/<platform>/godotjs-ext.exe).
	HMODULE current_module = nullptr;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(reinterpret_cast<void *>(&NodeBridge::PrepareNativeAddonHost)), &current_module)) {
		return {};
	}
	const String module_path = get_module_file_name(current_module);
	if (module_path.is_empty()) {
		return {};
	}
	const String dir = module_path.get_base_dir();
	if (dir.is_empty()) {
		return {};
	}
	return dir.path_join("godotjs-ext.exe");
#elif defined(LINUX_ENABLED) || defined(MACOS_ENABLED)
	// resolve the module directory through dladdr and look for the helper next
	// to it; on macOS also check the packaged .framework layout used by exports.
	const String module_path = get_module_file_name(reinterpret_cast<void *>(&NodeBridge::PrepareNativeAddonHost));
	if (module_path.is_empty()) {
		return {};
	}
	const String dir = module_path.get_base_dir();
	if (dir.is_empty()) {
		return {};
	}
	const String helper = dir.path_join("godotjs-ext");
	if (native_file_exists(helper)) {
		make_native_executable(helper);
		return helper;
	}
#	if defined(MACOS_ENABLED)
	const String plugin_helper = dir.path_join("../PlugIns/godotjs-ext.framework/godotjs-ext");
	if (native_file_exists(plugin_helper)) {
		make_native_executable(plugin_helper);
		return plugin_helper;
	}
#	endif
	return {};
#else
	// android/ios ship no helper (no fork / no dynamic .node loading).
	return {};
#endif
}

// [v8 api] return the absolute path of the standalone node helper executable
// used as the execPath for child_process.fork() probes, or null when the
// platform ships no helper.
void native_probe_executable(const v8::FunctionCallbackInfo<v8::Value> &info) {
	v8::Isolate *isolate = info.GetIsolate();
	const String path = get_native_probe_executable_path();
	if (path.is_empty()) {
		info.GetReturnValue().SetNull();
		return;
	}
	info.GetReturnValue().Set(Helper::new_string(isolate, path));
}

// context-aware registration of the 'godot' linked binding.
// Called lazily when the bootstrap script (or user code) accesses the binding.
void RegisterGodotBinding(v8::Local<v8::Object> exports, v8::Local<v8::Value> module, v8::Local<v8::Context> context, void *priv) {
	v8::Isolate *isolate = context->GetIsolate();
	exports->Set(context, Helper::new_string_ascii(isolate, "fs_readFile"), Helper::NewFunction(context, "fs_readFile", fs_read_file, v8::Local<v8::Value>())).Check();
	exports->Set(context, Helper::new_string_ascii(isolate, "fs_stat"), Helper::NewFunction(context, "fs_stat", fs_stat, v8::Local<v8::Value>())).Check();
	exports->Set(context, Helper::new_string_ascii(isolate, "preload_dlls"), Helper::NewFunction(context, "preload_dlls", preload_dlls, v8::Local<v8::Value>())).Check();
	exports->Set(context, Helper::new_string_ascii(isolate, "native_probe_executable"), Helper::NewFunction(context, "native_probe_executable", native_probe_executable, v8::Local<v8::Value>())).Check();
}
} //namespace

void NodeBridge::PrepareNativeAddonHost() {
#ifdef WINDOWS_ENABLED
	// locate this host module (godotjs-ext DLL) and preload the node.dll shim
	// next to it: dynamically loaded .node addons import napi_* symbols which
	// are forwarded through the shim.
	HMODULE current_module = nullptr;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(reinterpret_cast<void *>(&NodeBridge::PrepareNativeAddonHost)), &current_module)) {
		return;
	}
	const String module_path = get_module_file_name(current_module);
	if (module_path.is_empty()) {
		return;
	}
	const String dir = module_path.get_base_dir();
	if (dir.is_empty()) {
		return;
	}
	LoadLibraryW(dir.path_join("node.dll").wide_string().get_data());
#elif defined(LINUX_ENABLED) || defined(MACOS_ENABLED) || defined(ANDROID_ENABLED)
	// promote the N-API symbols of the host module (which statically links
	// libnode) to global visibility so that .node addons can resolve them.
	promote_current_module_symbols(reinterpret_cast<void *>(&NodeBridge::PrepareNativeAddonHost));
#endif
}

void NodeBridge::AddGodotLinkedBinding(node::Environment *p_env) {
	PrepareNativeAddonHost();

	node::node_module mod = {};
	mod.nm_version = NODE_MODULE_VERSION;
	mod.nm_flags = node::ModuleFlags::kLinked;
	mod.nm_dso_handle = nullptr;
	mod.nm_filename = "<godotjs-ext>";
	mod.nm_register_func = nullptr;
	mod.nm_context_register_func = &RegisterGodotBinding;
	mod.nm_modname = "godot";
	mod.nm_priv = nullptr;
	mod.nm_link = nullptr;
	node::AddLinkedBinding(p_env, mod);
}

} //namespace jsb::impl
