#include "jsb_resource_loader.h"

#include "jsb_script_language.h"
#include "jsb_script.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/resource_uid.hpp>
#include <godot_cpp/classes/resource_loader.hpp>

namespace
{
    bool is_worker_script(const String& p_path)
    {
        return false
#if JSB_EXCLUDE_WORKER_RES_SCRIPTS
#   if JSB_USE_TYPESCRIPT
        || p_path.ends_with(String(".worker.") + JSB_TYPESCRIPT_EXT)
#   endif
        || p_path.ends_with(String(".worker.") + JSB_JAVASCRIPT_EXT)
        || p_path.ends_with(String(".worker.") + JSB_COMMONJS_EXT)
        || p_path.ends_with(String(".worker.") + JSB_MODULE_EXT)
#endif
        ;
    }

    bool is_test_script(const String& p_path)
    {
        return false
#if JSB_EXCLUDE_TEST_RES_SCRIPTS
#   if JSB_USE_TYPESCRIPT
        || p_path.ends_with(String(".test.") + JSB_TYPESCRIPT_EXT)
#   endif
        || p_path.ends_with(String(".test.") + JSB_JAVASCRIPT_EXT)
        || p_path.ends_with(String(".test.") + JSB_COMMONJS_EXT)
        || p_path.ends_with(String(".test.") + JSB_MODULE_EXT)
#endif
        ;
    }

}

Variant ResourceFormatLoaderGodotJSScript::_load(const String& p_path, const String& p_original_path, bool p_use_sub_threads, int32_t p_cache_mode) const
{
    JSB_BENCHMARK_SCOPE(ResourceFormatLoaderGodotJSScript, _load);

    // {
    //     //TODO a dirty but approaching solution for hot-reloading
    //     std::lock_guard lock(GodotJSScriptLanguage::singleton_->mutex_);
    //     SelfList<GodotJSScript> *elem = GodotJSScriptLanguage::singleton_->script_list_.first();
    //     while (elem)
    //     {
    //         //TODO need to handle duplicate scripts if GodotJSScript is implemented as thread-wide (not implemented yet)
    //         if (elem->self()->get_path() == p_path)
    //         {
    //             if (p_cache_mode != CACHE_MODE_REUSE)
    //             {
    //                 elem->self()->load_source_code_from_path();
    //             }
    //
    //             //TODO temporarily ignore it, we are trying to implement scripts in worker threads which may be better not to reuse an existing script reference
    //             if (p_cache_mode == CACHE_MODE_REUSE)
    //             {
    //                 return Ref(elem->self());
    //             }
    //         }
    //         elem = elem->next();
    //     }
    // }

#ifdef TOOLS_ENABLED
    // only check the source file in editor mode since .ts source code is not required in runtime mode
    if (Engine::get_singleton()->is_editor_hint() && !FileAccess::file_exists(p_path))
    {
        return Variant();
    }
#endif
    jsb_check(p_path.ends_with(JSB_TYPESCRIPT_EXT) || p_path.ends_with(JSB_JAVASCRIPT_EXT) || p_path.ends_with(JSB_COMMONJS_EXT) || p_path.ends_with(JSB_MODULE_EXT));

    // in case `node_modules` is not ignored (which is not expected though), we do not want any GodotJSScript to be generated from it.
    if (p_path.begins_with("res://node_modules"))
    {
        return Variant();
    }

    // ignore DTS files, and worker scripts if they end with `.worker.js/ts`
    if (p_path.ends_with(String(".") + JSB_DTS_EXT) || is_worker_script(p_path) || is_test_script(p_path))
    {
        JSB_LOG(VeryVerbose, "excluding script resource %s", p_path);
        return Variant();
    }
    JSB_LOG(VeryVerbose, "loading script resource %s on thread %s", p_path, uitos(OS::get_singleton()->get_thread_caller_id()));

    // we can't immediately compile the script here since it's possibly loaded from resource loading threads
    switch (static_cast<ResourceLoader::CacheMode>(p_cache_mode))
    {
        case ResourceLoader::CACHE_MODE_IGNORE:
        case ResourceLoader::CACHE_MODE_IGNORE_DEEP:
            // the ResourceCache warning is really annoying,
            // we just ignore it here and let it behave like REUSE.
            // seems safe because GodotJSScript is stateless now (but must get script class info in a proper thread).
        case ResourceLoader::CACHE_MODE_REUSE:
            {
                if (const Ref<Resource> existing = ResourceLoader::get_singleton()->get_cached_ref(p_path);
                    existing.is_valid())
                {
                    jsb_check(existing->get_class() == jsb_typename(GodotJSScript));
                    jsb_check(existing->get_path() == p_path);
                    return existing;
                }
            }
            break;
        case ResourceLoader::CACHE_MODE_REPLACE:
        case ResourceLoader::CACHE_MODE_REPLACE_DEEP:
            break;
    }

    Ref<GodotJSScript> spt = Ref(memnew(GodotJSScript));
    const Error err = spt->load_source_code(p_path);
    if (err != OK)
    {
        JSB_LOG(Error, "failed to load script resource %s", p_path);
        return Variant();
    }
    spt->set_path(p_path);
    return spt;
}

PackedStringArray ResourceFormatLoaderGodotJSScript::_get_recognized_extensions() const
{
    PackedStringArray extensions;
#if JSB_USE_TYPESCRIPT
    extensions.push_back(JSB_TYPESCRIPT_EXT);
#endif
    extensions.push_back(JSB_JAVASCRIPT_EXT);
    extensions.push_back(JSB_COMMONJS_EXT);
    extensions.push_back(JSB_MODULE_EXT);
    return extensions;
}

bool ResourceFormatLoaderGodotJSScript::_handles_type(const StringName& p_type) const
{
    return p_type == StringName("Script") || p_type == StringName(jsb_typename(GodotJSScript));
}

String ResourceFormatLoaderGodotJSScript::_get_resource_type(const String& p_path) const
{
    const String el = p_path.get_extension().to_lower();

#if JSB_USE_TYPESCRIPT
    if (el == JSB_TYPESCRIPT_EXT || el == JSB_JAVASCRIPT_EXT || el == JSB_COMMONJS_EXT || el == JSB_MODULE_EXT)
#else
    if (el == JSB_JAVASCRIPT_EXT || el == JSB_COMMONJS_EXT || el == JSB_MODULE_EXT)
#endif // JSB_USE_TYPESCRIPT
    {
        return !is_worker_script(p_path) && !is_test_script(p_path)
            ? jsb_typename(GodotJSScript)
            : "";
    }
    return "";
}

PackedStringArray ResourceFormatLoaderGodotJSScript::_get_dependencies(const String& p_path, bool p_add_types) const
{
    //TODO
    return {};
}


#define UID_COMMENT_PREFIX "// uid://"
static int64_t extract_uid_from_line(const String &p_line) {
	PackedStringArray splits = p_line.strip_edges().substr(3).split(" ", false, 1);
	if (splits.is_empty()) {
		return ResourceUID::INVALID_ID;
	}
	return ResourceUID::get_singleton()->text_to_id(splits[0]);
}

int64_t ResourceFormatLoaderGodotJSScript::_get_resource_uid(const String &p_path) const {
	int64_t uid = ResourceUID::INVALID_ID;

	if (FileAccess::file_exists(p_path + String(".uid"))) {
		Ref<FileAccess> file = FileAccess::open(p_path + String(".uid"), FileAccess::READ);
		if (file.is_valid()) {
			uid = ResourceUID::get_singleton()->text_to_id(file->get_line());
		}
	} else {
		const String extension = p_path.get_extension().to_lower();
		if (extension == "ts") {
			Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ);
			if (file.is_valid()) {
				while (!file->eof_reached()) {
					String line = file->get_line().strip_edges();
					if (!line.is_empty()) {
						if (line.begins_with(UID_COMMENT_PREFIX)) {
							uid = extract_uid_from_line(line);
						}
						break;
					}
				}
			}
		}
	}

	return uid;
}

// bool ResourceFormatLoaderGodotJSScript::has_custom_uid_support() const {
// 	return jsb::internal::Settings::is_script_inline_resource_uid();
// }