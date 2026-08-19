/************************************************************************/
/*  jsb_module.h                                                        */
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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU   */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once

#include "jsb_bridge_pch.h"

namespace jsb {
class Environment;

namespace ModuleReloadResult {
enum Type : uint8_t {
	NoSuchModule,
	NoChanges,
	Requested,
	Disabled,
};
}

struct ModuleSourceInfo {
	// source file path (also used as the moduleid in module_cache)
	String source_filepath;

	// [optional] filepath of package.json which the source file is indirectly resolved from
	String package_filepath;
};

struct JavaScriptModule {
	StringName id;

	// asset path
	ModuleSourceInfo source_info;

	v8::Global<v8::Object> module;
	v8::Global<v8::Value> exports;

	// the default class exported in this JS module
	ScriptClassID script_class_id;

#if JSB_SUPPORT_RELOAD && defined(TOOLS_ENABLED)
	bool reload_requested = false;
	uint64_t time_modified = 0;
	String hash;

	_FORCE_INLINE_ bool is_reloading() const { return reload_requested; }

	// can't reload modules if it's time_modified is unknown or non-file modules
	bool is_reloadable() const { return time_modified != 0 && !source_info.source_filepath.is_empty(); }
#else
	_FORCE_INLINE_ constexpr bool is_reloading() const { return false; }
	_FORCE_INLINE_ constexpr bool is_reloadable() const { return false; }
#endif

	void on_load(v8::Isolate *isolate, const v8::Local<v8::Context> &context);
	bool mark_as_reloading();
	void mark_as_reloaded();
};

struct JavaScriptModuleCache {
private:
	friend class Environment;

	StringName main_;
	HashMap<StringName, JavaScriptModule *> modules_;
	v8::Global<v8::Object> cache_object_;

public:
	void init(v8::Isolate *isolate, const v8::Local<v8::Object> &cache_obj) {
		cache_object_.Reset(isolate, cache_obj);
	}

	void deinit() {
		cache_object_.Reset();
		for (const KeyValue<StringName, JavaScriptModule *> &it : modules_) {
			memdelete(it.value);
		}
		modules_.clear();
	}

	_FORCE_INLINE_ ~JavaScriptModuleCache() {
		jsb_check(cache_object_.IsEmpty());
		jsb_check(modules_.is_empty());
	}

	_FORCE_INLINE_ JavaScriptModule *find(const StringName &p_name) const {
		JavaScriptModule *const *it = modules_.getptr(p_name);
		return it ? *it : nullptr;
	}

	_FORCE_INLINE_ JavaScriptModule *get_main() const {
		return find(main_);
	}

	_FORCE_INLINE_ bool is_main(const StringName &p_name) const {
		return p_name == main_;
	}

	_FORCE_INLINE_ v8::Local<v8::Object> get_cache(v8::Isolate *isolate) const {
		return cache_object_.Get(isolate);
	}

	JavaScriptModule &insert(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const StringName &p_name, bool p_main_candidate, bool p_init_loaded);
};

} //namespace jsb

