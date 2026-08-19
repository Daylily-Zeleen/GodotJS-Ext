/************************************************************************/
/*  jsb_async_module_manager.h                                          */
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
#include "jsb_async_module_loader.h"
#include "jsb_bridge_pch.h"

namespace jsb {
//TODO handle parent module id in AsyncModuleManager?

/** a simple async module manager implementation */
class AsyncModuleManager {
	struct ModuleInfo {
		StringName module_id;

#if JSB_SUPPORT_ASYNC_MODULE_LOADER
		/** a Promise created by `import`. */
		v8::Global<v8::Promise::Resolver> resolver;
#endif
	};

	mutable std::recursive_mutex modules_mutex_;

	/**
	 * > THIS IS NOT IMPLEMENTED YET:
	 * > ModuleCache need to complete an async op by module_id if it is synchronously loaded
	 * > before IAsyncModuleLoader complete the loading.
	 * > The AsyncModuleHandle becomes invalid in this situation.
	 *
	 * This map is only used to accelerate the lookup of the module id.
	 */
	HashMap<StringName, AsyncModuleToken> tokens_;

	std::shared_ptr<IAsyncModuleLoader> loader_;

	internal::SArray<ModuleInfo, AsyncModuleToken> modules_;

public:
	AsyncModuleManager();
	~AsyncModuleManager();

	/** [threaded] */
	bool is_valid(AsyncModuleToken p_token) const;

	/** call by IAsyncModuleLoader */
	void _mark_as_handled(const v8::Local<v8::Context> &p_context, AsyncModuleToken p_token, bool p_is_fulfill, const v8::Local<v8::Value> &p_value);

	/** exposed JS function */
	static void _set_async_module_loader(const v8::FunctionCallbackInfo<v8::Value> &info);

	/** exposed JS function */
	static void _import(const v8::FunctionCallbackInfo<v8::Value> &info);

	/** */
	void set_loader(const std::shared_ptr<IAsyncModuleLoader> &p_loader);
};
} //namespace jsb
