#ifndef GODOTJS_SCRIPT_H
#define GODOTJS_SCRIPT_H

#include "../compat/jsb_compat.h"
#include "../bridge/jsb_bridge.h"

#include <godot_cpp/classes/script_extension.hpp>
#include <godot_cpp/classes/script_language.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/templates/self_list.hpp>
#include <godot_cpp/templates/rb_set.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/list.hpp>
#include <godot_cpp/templates/vector.hpp>

#include "jsb_script_language.h"

class ScriptInstance;
class PlaceholderScriptInstance;

class GodotJSScript : public ScriptExtension
{
    GDCLASS(GodotJSScript, ScriptExtension)

    friend class GodotJSScriptInstance;
    friend class GodotJSScriptInstanceBase;
    friend class GodotJSShadowScriptInstance;

private:
    bool loaded_ = false;

    bool source_changed_cache = false;
    String source_;

    Ref<GodotJSScript> base;

    // WTF??
    HashMap<StringName, Variant> member_default_values_cache;
    List<PropertyInfo> members_cache;

    // [INTERNAL] a self linked list to all GodotJSScript (lock is required to access)
    // 'script_class_info_' may be got from another environment,
    // so, explicitly load the module again if you want to create a GodotJSScriptInstance (instead of finding from module cache)
    SelfList<GodotJSScript> script_list_;

    RBSet<Object*> instances_;

    /**
     * 'StatelessScriptClassInfo' itself can be used without an environment,
     * Because we want GodotJSScript can be shared between threads (with a few restrictions, still need to be loaded in a proper thread).
     * So, you still need an environment to generate it (we do not implement a standalone script parser for simplicity).
     * @note valid only if loaded_ is true
     */
    jsb::StatelessScriptClassInfo script_class_info_;

#ifdef DEBUG_ENABLED
	HashMap<ObjectInstanceID, ScriptInstancePropertyState> pending_reload_state_;
    LocalVector<PlaceholderScriptInstance*, int32_t> placeholders; // TODO: 是否要改成 HashMap 加快查找？
#endif

#ifdef TOOLS_ENABLED
    jsb::ScriptPropertyInfo class_category_;
#endif // TOOLS_ENABLED

    // 允许 GodotJSScriptLanguage::reload_scripts_internal 访问 instances_, pending_reload_state_, placeholders
    friend void GodotJSScriptLanguage::reload_scripts_internal(const Array& p_scripts, bool p_soft_reload);
    friend void GodotJSScriptLanguage::finalize_instances_of_env(jsb::Environment* p_env); // HACK

private:
    void load_module_immediately();
    jsb_force_inline void ensure_module_loaded() const { if (jsb_unlikely(!loaded_)) const_cast<GodotJSScript*>(this)->load_module_immediately(); }
    jsb_force_inline bool is_valid_internal() const { return jsb::internal::VariantUtil::is_valid_name(script_class_info_.module_id); }

    Variant _new(const Variant** p_args, GDExtensionInt p_argcount, GDExtensionCallError &r_error);

#ifdef DEBUG_ENABLED
    bool _update_exports_internal(class PlaceholderScriptInstance* p_placeholder_instance_to_update);
    void _update_exports_values(TypedArray<Dictionary>& r_props, Dictionary& r_values);
#endif // DEBUG_ENABLED

    void remove_script_instance_instance_owner(Object *p_owner);
    GodotJSScriptInstance *try_create_script_instance(Object *p_owner, jsb::JSEnvironment &p_env, jsb::ScriptClassID p_script_class_id, auto P_bind_and_get_native_object_id);
public:
    GodotJSScript();
    virtual ~GodotJSScript() override;

	bool is_root_script() const { return _get_base_script().is_null(); }

    StringName get_module_id() const { return script_class_info_.module_id; };

    // Error attach_source(const String& p_path, bool p_take_over);
    Error load_source_code(const String &p_path);
    void load_module_if_missing();

    // Creates a ScriptInstance (for an existing Godot native object) and associates the ScriptInstance with an existing JS object (instance of the script's JS class).
    ScriptInstance* instance_create(const v8::Local<v8::Object>& p_this, Object* p_owner, bool p_is_temp_allowed);

    // Creates a ScriptInstance (and a NEW Godot native object) and associates the ScriptInstance with an existing JS object (instance of the script's JS class).
    ScriptInstance* instance_and_native_object_create(const v8::Local<v8::Object>& p_this, bool p_is_temp_allowed);

    // Creates a ScriptInstance and associates it with a newly constructed JS object (instance of script's class).
    ScriptInstance* instance_construct(Object* p_this, bool p_is_temp_allowed, const Variant **p_args = nullptr, int p_argcount = 0);

    // Creates a ScriptInstance and associates it with a newly constructed JS object (instance of script's class) without argument, and allow create a temporary shadow envrionment.
    ScriptInstance* instance_construct_default(Object* p_for_object) { return instance_construct(p_for_object, true); }

#pragma region Script Implementation
    virtual bool _can_instantiate() const override;

    virtual Ref<Script> _get_base_script() const override; // for script inheritance
    virtual StringName _get_global_name() const override;
    virtual bool _inherits_script(const Ref<Script>& p_script) const override;

    virtual StringName _get_instance_base_type() const override; // this may not work in all scripts, will return empty if so

    virtual GDExtensionScriptInstancePtr _instance_create(Object* p_for_object) const override;
    virtual GDExtensionScriptInstancePtr _placeholder_instance_create(Object* p_for_object) const override;
#ifdef TOOLS_ENABLED
    virtual void _placeholder_erased(GDExtensionScriptInstancePtr p_placeholder) override;
#endif // TOOLS_ENABLED

    virtual bool _has_source_code() const override { return !source_.is_empty(); }
    virtual String _get_source_code() const override { return source_; }
    virtual void _set_source_code(const String& p_code) override;

    virtual Error _reload(bool p_keep_state) override;

#ifdef TOOLS_ENABLED
    const jsb::ScriptPropertyInfo &get_class_category() const;
    virtual StringName _get_doc_class_name() const override;
    virtual TypedArray<Dictionary> _get_documentation() const override;
    virtual String _get_class_icon_path() const override;
#endif // TOOLS_ENABLED

    // TODO: In the next compat breakage rename to `*_script_*` to disambiguate from `Object::has_method()`.
    virtual bool _has_method(const StringName& p_method) const override;
    virtual bool _has_static_method(const StringName& p_method) const override;

    virtual Dictionary _get_method_info(const StringName& p_method) const override;

    // we expect Godot calling this after loaded_?
    // is_valid() will ensure the module is loaded.
    // [INTERNAL] if it's not expected, call `_is_valid` instead.
    virtual bool _is_valid() const override { ensure_module_loaded(); return is_valid_internal(); }
    virtual bool _is_tool() const override { return _is_valid() && script_class_info_.is_tool(); }
    virtual bool _is_abstract() const override { return _is_valid() && script_class_info_.is_abstract(); }

    virtual ScriptLanguage* _get_language() const override;

    virtual TypedArray<Dictionary> _get_script_property_list() const override;
    virtual TypedArray<Dictionary> _get_script_method_list() const override;
    virtual TypedArray<Dictionary> _get_script_signal_list() const override;

    virtual bool _has_script_signal(const StringName& p_signal) const override;

    virtual bool _is_placeholder_fallback_enabled() const override { return loaded_ && !_is_valid(); }
	virtual bool _has_property_default_value(const StringName &p_property) const override;
    virtual Variant _get_property_default_value(const StringName& p_property) const override;

    virtual void _update_exports() override;

    //editor tool
	virtual Variant _get_script_method_argument_count(const StringName &p_method) const override;

    virtual int32_t _get_member_line(const StringName& p_member) const override { return -1; } // TODO

    virtual Dictionary _get_constants() const override  // TODO
    {
        return Dictionary();
    }
    virtual TypedArray<StringName> _get_members() const override  // TODO
    {
        return TypedArray<StringName>();
    }

    virtual Variant _get_rpc_config() const override;

    virtual bool _editor_can_reload_from_file() override { return true; }

#pragma endregion // Script Interface Implementation

protected:
    static void _bind_methods();

public:
    // TODO: 是否只在调试模式下可用？
    template<typename ElemTy, ElemTy (*ConvertFn)(const jsb::ScriptPropertyInfo &), typename ListTy>
    requires requires(ListTy list, ElemTy elem) { list.push_back(elem);}
    void get_script_property_list(ListTy &r_list) const {
        ensure_module_loaded();
        jsb_check(loaded_);

#ifdef TOOLS_ENABLED
        r_list.push_back(ConvertFn(get_class_category()));
#endif
        for (const auto& it : script_class_info_.properties)
        {
            r_list.push_back(ConvertFn(it.value));
        }

        if (base.is_valid() && base->_is_valid())
        {
            base->get_script_property_list<ElemTy, ConvertFn, ListTy>(r_list);
        }
    }

    template<typename ElemTy, ElemTy (*ConvertFn)(const StringName &, const jsb::ScriptMethodInfo &), typename ListTy>
    requires requires(ListTy list, ElemTy elem) { list.push_back(elem);}
    void get_script_method_list(ListTy &r_list) const {
        ensure_module_loaded();
        jsb_check(loaded_);

        for (const auto& it : script_class_info_.methods)
        {
            r_list.push_back(ConvertFn(it.key, it.value));
        }

        if (base.is_valid() && base->_is_valid())
        {
            base->get_script_method_list<ElemTy, ConvertFn, ListTy>(r_list);
        }
    }

    template<typename ElemTy, ElemTy (*ConvertFn)(const StringName &, const jsb::ScriptSignalInfo &), typename ListTy>
    requires requires(ListTy list, ElemTy elem) { list.push_back(elem);}
    void get_script_signal_list(ListTy &r_list) const {
        if (!_is_valid()) return;

        for (const auto& it : script_class_info_.signals)
        {
            r_list.push_back(ConvertFn(it.key, it.value));
        }

        if (base.is_valid())
        {
            base->get_script_signal_list<ElemTy, ConvertFn, ListTy>(r_list);
        }
    }
};

#endif
