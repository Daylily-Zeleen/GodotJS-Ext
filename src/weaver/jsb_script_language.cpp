#include "jsb_script_language.h"

#include <iterator>

#include "jsb_monitor.h"
#include "../jsb_project_preset.h"
#include "../internal/jsb_internal.h"
#include "../bridge/jsb_worker.h"

#if JSB_SHADOW_REALM_ENABLED
#include "../bridge/jsb_shadow_realm.h"
#endif // JSB_SHADOW_REALM_ENABLED

#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_format_loader.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/scene_state.hpp>
#include <godot_cpp/classes/reg_ex_match.hpp>

#include "jsb_script.h"
#include "jsb_script_instance.h"

#ifdef TOOLS_ENABLED
#include "../weaver-editor/templates/templates.gen.h"
#endif

GodotJSScriptLanguage* GodotJSScriptLanguage::singleton_ = nullptr;

namespace jsb
{
    void JSEnvironment::init()
    {
        if (is_shadow_ && !target_)
        {
            target_ = GodotJSScriptLanguage::get_singleton()->create_shadow_environment();
        }
    }

    JSEnvironment::JSEnvironment(const String& p_path_hint, bool p_is_shadow_allowed)
    {
        target_ = jsb::Environment::_access();
        if (target_)
        {
            is_shadow_ = false;
        }
        else
        {
            jsb_ensuref(p_is_shadow_allowed, "no available Environment on thread %d for %s: %s", OS::get_singleton()->get_thread_caller_id(), jsb_typename(GodotJSScript), p_path_hint);
            is_shadow_ = true;
        }
    }

    JSEnvironment::~JSEnvironment()
    {
        if (is_shadow_ && target_)
        {
            GodotJSScriptLanguage::get_singleton()->destroy_shadow_environment(target_);
        }
    }
}

GodotJSScriptLanguage::GodotJSScriptLanguage()
{
    JSB_BENCHMARK_SCOPE(GodotJSScriptLanguage, Construct);
    jsb_check(!singleton_);
    singleton_ = this;
    jsb::internal::StringNames::create();
}

GodotJSScriptLanguage::~GodotJSScriptLanguage()
{
    jsb_check(!environment_);

    jsb::internal::StringNames::free();
    jsb_check(singleton_ == this);
    singleton_ = nullptr;

    // TODO: manage script list in a safer way (access and ref with script.id)
    std::lock_guard lock(mutex_);
    while (SelfList<GodotJSScript>* script_el = script_list_.first())
    {
        script_el->remove_from_list();
    }
}

void GodotJSScriptLanguage::finalize_instances_of_env(jsb::Environment* p_env) {
    HashSet<StringName> module_id_set;
    for (const auto &E : p_env->get_script_classes()) {
        module_id_set.insert(E.module_id);
    }

    std::lock_guard lock(mutex_);
    while (SelfList<GodotJSScript>* script_el = script_list_.first())
    {
        GodotJSScript* script = script_el->self();
        if (!module_id_set.has(script->script_class_info_.module_id)) continue;
        for (Object* obj : script->instances_) {
            GodotJSScriptInstance* si = ScriptInstance::get_script_instance<GodotJSScriptInstance>(obj);
            if (si->get_env() == p_env) si->env_ = nullptr;
        }
    }
}

void GodotJSScriptLanguage::_init()
{
    if (once_inited_) return;

    js_class_name_matcher1_ = RegEx::create_from_string(R"(\s*exports.default\s*=\s*class\s*(\w+)\s+extends\s+(\w+))");
    js_class_name_matcher2_ = RegEx::create_from_string(R"(\s*exports.default\s*=\s*(\w+)\s*;?)");
    ts_class_name_matcher_ = RegEx::create_from_string(R"(\s*(@[tT]ool\s*\(\s*\)\s*\n*\s*)?export\s+default\s+class\s+(\w+)(\s*<)?[^\n]*(?:>|\s+)extends\s+(\w+))");
    jsb_check(js_class_name_matcher1_.is_valid());
    jsb_check(js_class_name_matcher2_.is_valid());
    jsb_check(ts_class_name_matcher_.is_valid());

    JSB_BENCHMARK_SCOPE(GodotJSScriptLanguage, init);
    once_inited_ = true;
    JSB_LOG(Verbose, "Runtime: %s", JSB_IMPL_VERSION_STRING);
    JSB_LOG(VeryVerbose, "jsb lang init");

    jsb::Environment::CreateParams params;
    params.initial_class_slots = (int) ClassDBSingleton::get_singleton()->get_class_list().size() + JSB_MASTER_INITIAL_CLASS_EXTRA_SLOTS;
    params.initial_object_slots = JSB_MASTER_INITIAL_OBJECT_SLOTS;
    params.initial_script_slots = JSB_MASTER_INITIAL_SCRIPT_SLOTS;
    params.debugger_port = jsb::internal::Settings::get_debugger_port();
    params.thread_id = OS::get_singleton()->get_thread_caller_id();

    // main environment
    environment_ = std::make_shared<jsb::Environment>(params);
    environment_->init();

    if (const String entry_script_path = jsb::internal::Settings::get_entry_script_path();
        !entry_script_path.is_empty())
    {
        environment_->load(entry_script_path);
    }

#if JSB_DEBUG
    if (jsb::compat::Performance::get_singleton()) monitor_ = memnew(GodotJSMonitor);
#endif
}

void GodotJSScriptLanguage::_finish()
{
    jsb_check(once_inited_);

    js_class_name_matcher1_.unref();
    js_class_name_matcher2_.unref();
    ts_class_name_matcher_.unref();

#if JSB_DEBUG
    if (monitor_) memdelete(monitor_);
#endif
    once_inited_ = false;

#if !JSB_WITH_WEB
    jsb::Worker::finish();
#endif

#if JSB_SHADOW_REALM_ENABLED
    jsb::ShadowRealm::finish_all();
#endif // JSB_SHADOW_REALM_ENABLED
    {
        std::vector<ShadowEnvironment> shadow_environments;
        {
            std::lock_guard shadow_lock(shadow_mutex_);
            shadow_environments = shadow_environments_;
            shadow_environments_.clear();
        }
        for (const ShadowEnvironment& env : shadow_environments)
        {
            env.holder->dispose();
        }
    }

    // Now safe to dispose the main environment
    environment_->dispose();
    environment_.reset();

    JSB_LOG(VeryVerbose, "jsb lang finish");
}

void GodotJSScriptLanguage::_frame()
{
    const uint64_t base_ticks = Time::get_singleton()->get_ticks_msec();
    const uint64_t elapsed_milli = (base_ticks - last_ticks_); // milliseconds

    last_ticks_ = base_ticks;
    environment_->update(elapsed_milli);

#if JSB_DEBUG
    {
        std::lock_guard lock(mutex_);
        if (profile_info_map_.enabled)
        {
            for (auto& class_kv : profile_info_map_.classes)
            {
                for (auto& method_kv : class_kv.value.methods)
                {
                    method_kv.value.last_frame_calls = method_kv.value.frame_calls;
                    method_kv.value.last_frame_time = method_kv.value.frame_time;
                    method_kv.value.frame_calls = 0;
                    method_kv.value.frame_time = 0;
                }
            }
        }
    }
#endif
}

struct JavaScriptControlFlowKeywords
{
    HashSet<String> values;
    jsb_force_inline JavaScriptControlFlowKeywords()
    {
        constexpr static const char* _keywords[] =
        {
            "if", "else", "switch", "case", "do", "while", "for", "foreach",
            "return", "break", "continue",
            "try", "throw", "catch", "finally",
        };
        for (size_t index = 0; index < ::std::size(_keywords); ++index)
        {
            values.insert(_keywords[index]);
        }
    }
};

bool GodotJSScriptLanguage::_is_control_flow_keyword(const String &p_keyword) const
{
    static JavaScriptControlFlowKeywords collection;
    return collection.values.has(p_keyword);
}

PackedStringArray GodotJSScriptLanguage::_get_reserved_words() const
{
    return PackedStringArray {
        "return", "function", "interface", "class", "let", "break", "as", "any", "switch", "case", "if", "enum",
        "throw", "else", "var", "number", "string", "get", "module", "instanceof", "typeof", "public", "private",
        "while", "void", "null", "super", "this", "new", "in", "await", "async", "extends", "static",
        "package", "implements", "interface", "continue", "yield", "const", "export", "finally", "for",
        "import", "byte", "delete", "goto",
        "default",
    };
}

PackedStringArray GodotJSScriptLanguage::_get_doc_comment_delimiters() const
{
    return PackedStringArray { "///" };
}

PackedStringArray GodotJSScriptLanguage::_get_comment_delimiters() const
{
    return PackedStringArray { "//", "/* */" };
}

PackedStringArray GodotJSScriptLanguage::_get_string_delimiters() const
{
    return PackedStringArray { "' '", "\" \"", "` `" };
}

Dictionary GodotJSScriptLanguage::_validate(const String& p_script, const String& p_path, bool p_validate_functions, bool p_validate_errors, bool p_validate_warnings, bool p_validate_safe_lines) const
{
    Dictionary result;
    // TODO
    // "functions": PackedStringArray
    // "errors": Array[Dictionary] ScriptError {"line": int, "column": int, "message": String}
    // "warnings": Array[Dictionary] Warning {"start_line": int, "end_line": int, "code": int(Error Code), "string_code": String, "message": String}
    // "safe_lines": PackedInt32Array
    if (environment_->validate_script(p_path))
    {
        result["valid"] = true;
        return result;
    }

    //TODO parse error info
    result["valid"] = false;

    Dictionary err;
    err["line"] = 0;
    err["column"] = 0;
    err["message"] = "NOT_IMPLEMENTED";
    result["errors"] = Array::make(err);
    return result;
}

Ref<Script> GodotJSScriptLanguage::_make_template(const String& p_template, const String& p_class_name, const String& p_base_class_name) const
{
    Ref<GodotJSScript> spt;
    spt.instantiate();
    String processed_template = p_template;
    processed_template = processed_template.replace("_BASE_", p_base_class_name)
                                 .replace("_CLASS_SNAKE_CASE_", jsb::internal::VariantUtil::to_snake_case_id(p_class_name))
                                 .replace("_CLASS_", jsb::internal::VariantUtil::to_pascal_case_id(p_class_name))
                                 .replace("_TS_", jsb::internal::Settings::get_indentation());
    spt->_set_source_code(processed_template);
    return spt;
}

TypedArray<Dictionary> GodotJSScriptLanguage::_get_built_in_templates(const StringName& p_object) const
{
    TypedArray<Dictionary> templates;
#ifdef TOOLS_ENABLED
    for (const Dictionary& template_dict : (::get_script_templates()))
    {
        if (template_dict["inherit"] == p_object)
        {
            templates.append(template_dict);
        }
    }
#endif // TOOLS_ENABLED
    return templates;
}

struct GodotJSScriptDepSort {
	//must support sorting so inheritance works properly (parent must be reloaded first)
	bool operator()(const Ref<GodotJSScript> &A, const Ref<GodotJSScript> &B) const {
		if (A == B) {
			return false; //shouldn't happen but..
		}
		GodotJSScript *I = static_cast<GodotJSScript*>(B->_get_base_script().ptr());
		while (I) {
			if (I == A.ptr()) {
				// A is a base of B
				return true;
			}

			I = static_cast<GodotJSScript*>(I->_get_base_script().ptr());
		}

		return false; //not a base
	}
};

void GodotJSScriptLanguage::_reload_scripts(const Array& p_scripts, bool p_soft_reload)
{
    reload_scripts_internal(p_scripts, p_soft_reload);
}

void GodotJSScriptLanguage::_profiling_set_save_native_calls(bool p_enable)
{
    JSB_LOG(Verbose, "TODO [GodotJSScriptLanguage::_profiling_set_save_native_calls] NOT IMPLEMENTED");
}

void GodotJSScriptLanguage::_reload_all_scripts()
{
#ifdef DEBUG_ENABLED
	print_verbose("GodotJSScript: Reloading all scripts");
	Array scripts;
	{
		std::lock_guard lock(mutex_);

		SelfList<GodotJSScript> *elem = script_list_.first();
		while (elem) {
            const String script_path = elem->self()->get_path();
			if (script_path.begins_with("res://")) {
				print_verbose("GodotJSScript: Found: " + script_path);
				scripts.push_back(Ref<GodotJSScript>(elem->self())); //cast to gdscript to avoid being erased by accident
			}
			elem = elem->next();
		}

#ifdef TOOLS_ENABLED
        // TODO: Implement global mechanism.
#endif // TOOLS_ENABLED
	}

	reload_scripts_internal(scripts, true);
#endif // DEBUG_ENABLED
}

void GodotJSScriptLanguage::_reload_tool_script(const Ref<Script>& p_script, bool p_soft_reload)
{
	Array scripts;
    scripts.push_back(p_script);
	reload_scripts_internal(scripts, p_soft_reload);
}

PackedStringArray GodotJSScriptLanguage::_get_recognized_extensions() const
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


Dictionary GodotJSScriptLanguage::_get_global_class_name(const String& p_path) const
{
    // GodotJSScript implementation do not really support threaded access for now.
    // So, we can not load the script module in-place because `get_global_class_name` could be called from EditorFileSystem (background) scan.
    // And for simplicity, we use regex to extract the class name from the source code instead of using ANTLR or similar.
    // Please follow the rules of the class name declaration in the source code.
    //     * .ts files: `export default class ClassName extends BaseClassName`
    //     * .js files: `class ClassName extends BaseClassName` and `exports.default = ClassName` (with or without `;`)

    // And, we do not support abstract classes here, please define all abstract class by not exporting it as `default`.
    // It should be equivalent and enough for TS/JS since we do not rely on GodotJSScript to use abstract classes in TS/JS sources.

    Dictionary result;
    const Ref<FileAccess> file_access = FileAccess::open(p_path, FileAccess::READ);
    if (file_access.is_null())
    {
        return result;
    }

    const String source = file_access->get_as_text();
    String class_name;
    String base_type;
    String icon_path; // TODO
    bool is_tool {false};
    bool is_abstract {false}; // TODO
    if (jsb::internal::PathUtil::is_recognized_javascript_extension(p_path))
    {
        // check if the class id defined in a single line (export default class ClassName extends BaseClassName)
        jsb_check(js_class_name_matcher1_.is_valid());
        const Ref<RegExMatch> match1 = js_class_name_matcher1_->search(source);
        if (match1.is_valid() && match1->get_group_count() == 2)
        {
            class_name = match1->get_string(1);
            base_type = match1->get_string(2);
        }
        else
        {
            // otherwise, it probably defined in separated lines (firstly, check 'class ClassName extends BaseClassName')
            jsb_check(js_class_name_matcher2_.is_valid());
            const Ref<RegExMatch> match2 = js_class_name_matcher2_->search(source);
            if (match2.is_valid() && match2->get_group_count() == 1)
            {
                class_name = match2->get_string(1);
                // then, check 'exports.default = ClassName'
                const Ref<RegEx> base_matcher = RegEx::create_from_string(jsb::internal::format(R"(\s*class\s*%s\s*extends\s*(\w+)\s*\{?)", class_name));
                const Ref<RegExMatch> base_match = base_matcher->search(source);
                if (base_match.is_valid() && base_match->get_group_count() == 1)
                {
                    base_type = base_match->get_string(1);
                }
            }
        }
    }
    else
    {
        // hope it's a typescript file
        jsb_check(ts_class_name_matcher_.is_valid());

        Ref<RegExMatch> match = ts_class_name_matcher_->search(source);
        if (match.is_valid() && match->get_group_count() == 4)
        {
            is_tool = !match->get_string(1).is_empty();
            class_name = match->get_string(2);
            base_type = match->get_string(4);
        }
    }

    if (!class_name.is_empty())
    {
        result["class"] = class_name;
        result["base_type"] = base_type;
        result["icon_path"] = icon_path;
        result["is_tool"] = is_tool;
        result["is_abstract"] = is_abstract;
    }
    return result;
}

bool GodotJSScriptLanguage::_handles_global_class_type(const String& p_type) const
{
    return p_type == jsb_typename(GodotJSScript);
}

String GodotJSScriptLanguage::_get_name() const
{
    return jsb_typename(GodotJSScript);
}

String GodotJSScriptLanguage::_get_type() const
{
    return jsb_typename(GodotJSScript);
}

void GodotJSScriptLanguage::scan_external_changes()
{
    environment_->scan_external_changes();

#ifdef TOOLS_ENABLED
    // fix scripts with no .js counterpart found (only missing scripts)
    {
        std::lock_guard lock(mutex_);
        const SelfList<GodotJSScript>* elem = script_list_.first();
        while (elem)
        {
            elem->self()->load_module_if_missing();
            elem = elem->next();
        }
    }
#endif
}

void GodotJSScriptLanguage::_thread_enter()
{
    jsb::Worker::on_thread_enter();
}

void GodotJSScriptLanguage::_thread_exit()
{
    jsb::Worker::on_thread_exit();
}

void GodotJSScriptLanguage::_profiling_start()
{
#if JSB_DEBUG
    std::lock_guard lock(mutex_);
    profile_info_map_.enabled = true;
#endif
}

void GodotJSScriptLanguage::_profiling_stop()
{
#if JSB_DEBUG
    std::lock_guard lock(mutex_);
    profile_info_map_.enabled = false;
#endif
}

void GodotJSScriptLanguage::add_script_call_profile_info(const String& p_path, const StringName& p_class, const StringName& p_method, uint64_t p_time)
{
    // we only collect GodotJSScriptInstance function profiling data instead of the deep profiling data from JS runtime.
    // please use Chrome DevTools for deep JS profiling.

#if JSB_DEBUG
    std::lock_guard lock(mutex_);
    if (!profile_info_map_.enabled) return;

    ScriptClassProfileInfo& prof = profile_info_map_.classes[p_class];
    prof.path = p_path;
    prof.methods[p_method].frame_calls++;
    prof.methods[p_method].frame_time += p_time;
    prof.methods[p_method].total_calls++;
    prof.methods[p_method].total_time += p_time;
#endif
}

bool GodotJSScriptLanguage::is_global_class_generic(const String &p_path) const
{
    const Ref<FileAccess> file_access = FileAccess::open(p_path, FileAccess::READ);
    if (file_access.is_null())
    {
        return false;
    }

    const String source = file_access->get_as_text();

    if (jsb::internal::PathUtil::is_recognized_javascript_extension(p_path))
    {
        return false;
    }

    jsb_check(ts_class_name_matcher_.is_valid());

    Ref<RegExMatch> match = ts_class_name_matcher_->search(source);
    return match.is_valid() && match->get_group_count() == 4 && match->get_string(3).length() > 0;
}

namespace
{
    String to_signature(const String& p_path, const StringName& p_class, const StringName& p_method)
    {
        // path :: line :: class :: method
        return jsb_format("%s::0::%s::%s", p_path, p_class, p_method);
    }
}

int32_t GodotJSScriptLanguage::_profiling_get_accumulated_data(ScriptLanguageExtensionProfilingInfo* p_info_arr, int32_t p_info_max)
{
#if JSB_DEBUG
    std::lock_guard lock(mutex_);
    if (!profile_info_map_.enabled) return 0;

    int current = 0;
    for (const auto& class_kv : profile_info_map_.classes)
    {
        for (const auto& method_kv : class_kv.value.methods)
        {
            if (current >= p_info_max)
            {
                return current;
            }
            p_info_arr[current].signature = to_signature(class_kv.value.path, class_kv.key, method_kv.key);
            p_info_arr[current].self_time = method_kv.value.total_time;
            p_info_arr[current].total_time = method_kv.value.total_time;
            p_info_arr[current].call_count = method_kv.value.total_calls;
            current++;
        }
    }
    return current;
#else
    return 0;
#endif
}

int32_t GodotJSScriptLanguage::_profiling_get_frame_data(ScriptLanguageExtensionProfilingInfo* p_info_arr, int32_t p_info_max)
{
#if JSB_DEBUG
    std::lock_guard lock(mutex_);
    if (!profile_info_map_.enabled) return 0;

    int current = 0;
    for (const auto& class_kv : profile_info_map_.classes)
    {
        for (const auto& method_kv : class_kv.value.methods)
        {
            if (current >= p_info_max)
            {
                return current;
            }
            p_info_arr[current].signature = to_signature(class_kv.value.path, class_kv.key, method_kv.key);
            p_info_arr[current].self_time = method_kv.value.last_frame_time;
            p_info_arr[current].total_time = method_kv.value.last_frame_time;
            p_info_arr[current].call_count = method_kv.value.last_frame_calls;
            current++;
        }
    }
    return current;
#else
    return 0;
#endif
}

std::shared_ptr<jsb::Environment> GodotJSScriptLanguage::create_shadow_environment()
{
    const jsb::compat::ThreadID caller_id = OS::get_singleton()->get_thread_caller_id();
    {
        std::lock_guard shadow_lock(shadow_mutex_);

        for (ShadowEnvironment& shadow : shadow_environments_)
        {
            if (shadow.rc == 0 || shadow.thread_id == caller_id)
            {
                shadow.rc++;
                return shadow.holder;
            }
        }
    }

    jsb::Environment::CreateParams params;
    params.initial_class_slots = 128;
    params.initial_object_slots = 512;
    params.initial_script_slots = 32;
    params.type = jsb::Environment::Type::Shadow;
    params.thread_id = jsb::compat::UNASSIGNED_THREAD_ID;

    std::shared_ptr<jsb::Environment> env = std::make_shared<jsb::Environment>(params);
    JSB_LOG(Log, "creating a shadow Environment on thread %d for %s [env %s]",
        OS::get_singleton()->get_thread_caller_id(),
        jsb_typename(GodotJSScript),
        (uintptr_t) env->id());
    env->init();
    {
        std::lock_guard shadow_lock(shadow_mutex_);
        shadow_environments_.push_back({caller_id, env, 1});
    }
    return env;
}

void GodotJSScriptLanguage::destroy_shadow_environment(const std::shared_ptr<jsb::Environment>& p_env)
{
    bool found = false;
    bool should_dispose = false;
    {
        std::lock_guard shadow_lock(shadow_mutex_);
        const size_t num = shadow_environments_.size();
        for (auto it = shadow_environments_.begin();
            it != shadow_environments_.end();
            ++it)
        {
            if (it->holder == p_env)
            {
                found = true;
                if (--it->rc == 0 && num > JSB_MAX_CACHED_SHADOW_ENVIRONMENTS)
                {
                    should_dispose = true;
                    shadow_environments_.erase(it);
                }
                break;
            }
        }
    }
    jsb_checkf(found, "not a registered shadow environment");
    if (should_dispose) p_env->dispose();
}

void GodotJSScriptLanguage::reload_scripts_internal(const Array& p_scripts, bool p_soft_reload)
{
#ifdef DEBUG_ENABLED

	List<Ref<GodotJSScript>> scripts;
	{
		std::lock_guard lock(mutex_);

		SelfList<GodotJSScript> *elem = script_list_.first();
		while (elem) {
			// Scripts will reload all subclasses, so only reload root scripts.
			if (elem->self()->is_root_script() && !elem->self()->get_path().is_empty()) {
				scripts.push_back(Ref<GodotJSScript>(elem->self())); //cast to gdscript to avoid being erased by accident
			}
			elem = elem->next();
		}
	}

	//when someone asks you why dynamically typed languages are easier to write....

	HashMap<Ref<GodotJSScript>, HashMap<ObjectInstanceID, ScriptInstancePropertyState>> to_reload;

	//as scripts are going to be reloaded, must proceed without locking here

	scripts.sort_custom<GodotJSScriptDepSort>(); //update in inheritance dependency order

	for (Ref<GodotJSScript> &scr : scripts) {
		bool reload = p_scripts.has(scr) || to_reload.has(scr->get_base_script());

		if (!reload) {
			continue;
		}

		to_reload.insert(scr, HashMap<ObjectInstanceID, ScriptInstancePropertyState>());

		if (!p_soft_reload) {
			//save state and remove script from instances
			HashMap<ObjectInstanceID, ScriptInstancePropertyState> &map = to_reload[scr];

			while (scr->instances_.front()) {
				Object *obj = scr->instances_.front()->get();
				if (ScriptInstance* si = ScriptInstance::get_script_instance(obj)) {
                    //save instance info
                    ScriptInstancePropertyState state;
                    si->get_property_state(state);
					map.insert(obj->get_instance_id(), state);

					obj->set_script(Variant()); // NOTE: 脚本改变时会在 godot 侧释放旧的 script_instance.
				}
			}

#ifdef TOOLS_ENABLED
			//same thing for placeholders
            while (!scr->placeholders.is_empty()) {
                auto size = scr->placeholders.size();
                Object *obj = scr->placeholders[size - 1]->get_owner();
                if (ScriptInstance* si = ScriptInstance::get_script_instance(obj)) {
                    //save instance info
                    ScriptInstancePropertyState state;
                    si->get_property_state(state);
					map.insert(obj->get_instance_id(), state);

					obj->set_script(Variant()); // NOTE: 脚本改变时会在 godot 侧释放旧的 script_instance.
                } else {
                    scr->placeholders.resize(size - 1);
                }
            }

#endif // TOOLS_ENABLED

			for (const KeyValue<ObjectInstanceID, ScriptInstancePropertyState> &F : scr->pending_reload_state_) {
				map[F.key] = F.value; //pending to reload, use this one instead
			}
		}
	}

	for (KeyValue<Ref<GodotJSScript>, HashMap<ObjectInstanceID, ScriptInstancePropertyState>> &E : to_reload) {
		Ref<GodotJSScript> scr = E.key;
        const String scr_path = scr->get_path();
		print_verbose("GodotJSScript: Reloading: " + scr_path);
		if (scr->is_built_in()) {
			// TODO: It would be nice to do it more efficiently than loading the whole scene again.
			Ref<PackedScene> scene = ResourceLoader::get_singleton()->load(scr_path.get_slice("::", 0), "", ResourceLoader::CACHE_MODE_IGNORE_DEEP);
			ERR_CONTINUE(scene.is_null());

            auto get_subresource_script =  [] (const Ref<PackedScene> &p_scene, const String &p_path) -> Ref<GodotJSScript> {
                Ref<SceneState> state = p_scene->get_state();
                int32_t node_count = state->get_node_count();
                for (int32_t node_idx = 0; node_idx < node_count; ++node_idx)
                {
                    int32_t property_count = state->get_node_property_count(node_idx);
                    for (int32_t property_idx = 0; property_idx < property_count; ++property_idx)
                    {
                        if (GodotJSScript* maybe_resources = Object::cast_to<GodotJSScript>(state->get_node_property_value(node_idx, property_idx).get_validated_object()))
                        {
                            if (maybe_resources->get_path() == p_path)
                            {
                                return maybe_resources;
                            }
                        }
                    }
                }
                return Ref<GodotJSScript>();
            };

            Ref<GodotJSScript> fresh = get_subresource_script(scene, scr_path);
			ERR_CONTINUE(fresh.is_null());

			scr->load_source_code(scr_path);
		} else {
			scr->load_source_code(scr_path);
		}
		scr->reload(p_soft_reload);

		//restore state if saved
		for (KeyValue<ObjectInstanceID, ScriptInstancePropertyState> &F : E.value) {
			ScriptInstancePropertyState &saved_state = F.value;

			Object *obj = ObjectDB::get_instance(F.key);
			if (!obj) {
				continue;
			}

			if (!p_soft_reload) {
				//clear it just in case (may be a pending reload state)
				obj->set_script(Variant());
			}
			obj->set_script(scr);

            ScriptInstance* script_instance = ScriptInstance::get_script_instance(obj);
			if (script_instance == nullptr) {
				//failed, save reload state for next time if not saved
				if (!scr->pending_reload_state_.has(obj->get_instance_id())) {
					scr->pending_reload_state_[obj->get_instance_id()] = saved_state;
				}
				continue;
			}

			if (script_instance->is_placeholder() && scr->_is_placeholder_fallback_enabled()) {
				PlaceholderScriptInstance *placeholder = static_cast<PlaceholderScriptInstance *>(script_instance);
                for (const auto & G: saved_state) {
					// placeholder->property_set_fallback(G.first, G.second); // TODO: Godot 未暴露接口
				}
			} else {
                GodotJSScriptInstanceBase *si = static_cast<GodotJSScriptInstanceBase*>(script_instance);
                si->set_property_state(saved_state);
			}

			scr->pending_reload_state_.erase(obj->get_instance_id()); //as it reloaded, remove pending state
		}

		//if instance states were saved, set them!
	}

#endif // DEBUG_ENABLED
}


void GodotJSScriptLanguage::_bind_methods() {
}
