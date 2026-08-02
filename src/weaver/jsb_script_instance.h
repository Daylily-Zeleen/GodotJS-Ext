#ifndef GODOTJS_SCRIPT_INSTANCE_H
#define GODOTJS_SCRIPT_INSTANCE_H

#include "jsb_script_language.h"
#include "jsb_script.h"

#include <gdextension_interface.h>

class PlaceholderScriptInstance;
class GodotJSScriptInstanceBase;
class GodotJSShadowScriptInstance;
class GodotJSScriptInstance;

class ScriptInstance {
protected:
    Object* owner_ = nullptr;
    Ref<GodotJSScript> script_;
    GDExtensionScriptInstancePtr extension_instance_ptr {nullptr};

    ScriptInstance(const Ref<GodotJSScript> &p_script, Object* p_owner, GDExtensionScriptInstancePtr p_extension_instance_ptr);
public:
    const GDExtensionScriptInstancePtr get_extension_instance_ptr() const {return extension_instance_ptr;}
public:
    virtual ~ScriptInstance() = default;

    virtual bool is_placeholder() const { return false; }
    virtual bool is_shadow() const { return false;}

    // Owner/script/language access
    Object* get_owner() const { return owner_; }
    Ref<GodotJSScript> get_script() const { return script_; }
    ScriptLanguage* get_language() const { return script_.is_valid() ? GodotJSScriptLanguage::get_singleton() : nullptr; }

public:
    virtual void get_property_state(ScriptInstancePropertyState &p_state) const {}

public:
    static ScriptInstance* get_script_instance(Object* p_object);
    static void set_script_instance(Object *p_object, ScriptInstance *p_instance);

    template<typename ScriptInstanceTy>
    static ScriptInstanceTy* get_script_instance(Object* p_object) {
        ScriptInstance* si = get_script_instance(p_object);
        if (si == nullptr) return nullptr;

        if (si->is_placeholder()) {
            if constexpr (std::is_same_v<ScriptInstanceTy, PlaceholderScriptInstance>) return si;
            else return nullptr;
        }

        if constexpr (std::is_same_v<ScriptInstanceTy, GodotJSShadowScriptInstance>) {
            if (!(GodotJSScriptInstanceBase*)si->is_shadow()) return nullptr;
        }
        if constexpr (std::is_same_v<ScriptInstanceTy, GodotJSShadowScriptInstance>) {
            if ((GodotJSScriptInstanceBase*)si->is_shadow()) return nullptr;
        }
        return static_cast<ScriptInstanceTy*>(si);
    }
};


#ifdef TOOLS_ENABLED
class PlaceholderScriptInstance: public ScriptInstance
{
    static HashMap<Object *, PlaceholderScriptInstance *> placeholders_;
public:
    PlaceholderScriptInstance(const Ref<GodotJSScript> &p_script, Object* p_owner);
    virtual ~PlaceholderScriptInstance() override;

    static PlaceholderScriptInstance *try_get_placeholder_script_instance(Object *p_object) {
        PlaceholderScriptInstance **inst_ptr = placeholders_.getptr(p_object);
        return inst_ptr ? *inst_ptr : nullptr;
    }
    virtual bool is_placeholder() const override { return true; }
    void update(const TypedArray<Dictionary> &p_properties, const Dictionary &p_values);
};
#endif // TOOLS_ENABLED

// An abstract base class for GodotJS script instance implementations
class GodotJSScriptInstanceBase: public ScriptInstance
{
protected:
    GDExtensionScriptInstancePtr extension_instance_ptr {nullptr};

protected:
    struct ScriptProfilingInfo
    {
        String path_;
        StringName class_;
    };

    struct ScriptCallProfilingScope
    {
        const ScriptProfilingInfo& info_;
        StringName method_;
        uint64_t start_time_;

        ScriptCallProfilingScope(const ScriptProfilingInfo& p_info, const StringName& p_method);
        ~ScriptCallProfilingScope();
    };

protected:
#if JSB_DEBUG
    ScriptProfilingInfo profiling_info_;
#endif

protected:
    mutable LocalVector<PropertyInfo> *temporary_script_property_list_cache { nullptr };
    LocalVector<MethodInfo> *temporary_script_method_list_cache { nullptr };

    virtual LocalVector<PropertyInfo> *make_temporary_property_list() const ;
    void free_temporary_property_list() const { jsb_check(temporary_script_property_list_cache); memdelete(temporary_script_property_list_cache); temporary_script_property_list_cache = nullptr; }

    /**
     * @brief 回调专用，返回 temporary_script_method_list_cache
     *      NOTE: 注意会在回调处理结束后销毁 temporary_script_method_list_cache，重写该函数时不需要进行额外的内存管理
     * @return LocalVector<MethodInfo>* 
     */
    virtual LocalVector<MethodInfo> *make_temporary_method_list();
    void free_temporary_method_list() {jsb_check(temporary_script_method_list_cache); memdelete(temporary_script_method_list_cache); temporary_script_method_list_cache = nullptr;}

    GodotJSScriptInstanceBase(const Ref<GodotJSScript> &p_script, Object* p_owner);
    friend struct ScriptInstanceInfo;
public:
    virtual ~GodotJSScriptInstanceBase() override;

    // Property access
    virtual bool set(const StringName& p_name, const Variant& p_value) = 0;
    virtual bool get(const StringName& p_name, Variant& r_ret) const = 0;
    // virtual void get_property_list(LocalVector<GDExtensionPropertyInfo>* p_properties) const = 0;
    virtual Variant::Type get_property_type(const StringName& p_name, bool* r_is_valid = nullptr) const = 0;
    virtual void validate_property(PropertyInfo& p_property) const = 0;
    virtual bool property_can_revert(const StringName& p_name) const { return false; }
    virtual bool property_get_revert(const StringName& p_name, Variant& r_ret) const { return false; }
    virtual void get_property_state(ScriptInstancePropertyState &p_state) const override;
    // Method access
    virtual bool has_method(const StringName& p_method) const = 0;
	virtual int get_method_argument_count(const StringName &p_method, bool *r_is_valid = nullptr) const {return 0;} // TODO
    virtual Variant callp(const StringName& p_method, const Variant** p_args, int p_argcount, GDExtensionCallError& r_error) = 0;

    // Notifications
    virtual void notification(int p_notification, bool p_reversed = false) {}

    virtual String to_string(bool *r_valid);

    // RPC
    virtual const Variant get_rpc_config() const { return Variant(); }

public:
    void set_property_state(const ScriptInstancePropertyState &p_state) {
        for (const auto &kv : p_state) {
            this->set(kv.first, kv.second);
        }
    }
};

// A runtime placeholder for the script instances which instantiated by async resource loader request.
// It can only be used to store states, DO NOT invoke `callp()` anyway. And, all notifications are ignored.
// After the owner object is instantiated, the Environment will replace this shadow instance with a real GodotJSScriptInstance when binding the JS object.
class GodotJSShadowScriptInstance : public GodotJSScriptInstanceBase
{
private:
    HashMap<StringName, Variant> state_;

public:
    virtual bool is_shadow() const override { return true; }

#pragma region GodotJSScriptInstanceBase Implementation
    virtual bool set(const StringName& p_name, const Variant& p_value) override
    {
        state_[p_name] = p_value;
        return true;
    }

    virtual bool get(const StringName& p_name, Variant& r_ret) const override
    {
        if (const Variant* ptr = state_.getptr(p_name))
        {
            r_ret = *ptr;
            return true;
        }
        return false;
    }

    // virtual void get_property_list(LocalVector<GDExtensionPropertyInfo>* p_properties) const override;
    virtual Variant::Type get_property_type(const StringName& p_name, bool* r_is_valid = nullptr) const override;

    virtual void validate_property(PropertyInfo& p_property) const override {}
    virtual bool property_can_revert(const StringName& p_name) const override { return false; }
    virtual bool property_get_revert(const StringName& p_name, Variant& r_ret) const override { return false; }

    virtual bool has_method(const StringName& p_method) const override;

    virtual Variant callp(const StringName& p_method, const Variant** p_args, int p_argcount, GDExtensionCallError& r_error) override
    {
        r_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
        return {};
    }

    virtual void notification(int p_notification, bool p_reversed = false) override
    {
    }

    virtual const Variant get_rpc_config() const override;
#pragma endregion
public:
    GodotJSShadowScriptInstance(const Ref<GodotJSScript> &p_script, Object *p_owner):
        GodotJSScriptInstanceBase(p_script, p_owner) {}
};

class GodotJSScriptInstance : public GodotJSScriptInstanceBase
{
private:
    jsb::EnvironmentRef env_;

    // script class handle
    jsb::ScriptClassID class_id_;

    // object handle (the JS object binding id)
    jsb::NativeObjectID object_id_;

    HashMap<StringName, Variant> property_cache_;

private:

    jsb::ScriptClassInfoPtr get_script_class() const;

    friend GodotJSScriptInstance *GodotJSScript::try_create_script_instance(Object *p_owner, jsb::JSEnvironment &p_env, jsb::ScriptClassID p_script_class_id, auto P_bind_and_get_native_object_id);

protected:
    virtual LocalVector<PropertyInfo> *make_temporary_property_list() const override;

public:
    jsb_force_inline jsb::Environment *get_env() const { return env_; }
    jsb::compat::ThreadID get_env_thread_id() const { return env_ ? env_->get_thread_id() : jsb::compat::UNASSIGNED_THREAD_ID; }

    // for Environment lifecycle control (avoid object leaks), detach all JS object bindings
    // void _detach();

    void postbind();
    void cache_property(const StringName& name, const Variant& value);

#pragma region GodotJSScriptInstanceBase Implementation

    virtual bool set(const StringName& p_name, const Variant& p_value) override;
    virtual bool get(const StringName& p_name, Variant& r_ret) const override;
    // virtual void get_property_list(LocalVector<GDExtensionPropertyInfo>* p_properties) const override;
    virtual Variant::Type get_property_type(const StringName& p_name, bool* r_is_valid = nullptr) const override;
    virtual void validate_property(PropertyInfo& p_property) const override;

    virtual bool property_can_revert(const StringName& p_name) const override;
    virtual bool property_get_revert(const StringName& p_name, Variant& r_ret) const override;

    virtual bool has_method(const StringName& p_method) const override;
    virtual Variant callp(const StringName& p_method, const Variant** p_args, int p_argcount, GDExtensionCallError& r_error) override;

    virtual void notification(int p_notification, bool p_reversed = false) override;

    virtual const Variant get_rpc_config() const override;
#pragma endregion

public:
    GodotJSScriptInstance(const Ref<GodotJSScript> &p_script, Object *p_owner, jsb::JSEnvironment &p_env, const jsb::ScriptClassID & p_class_id):
        GodotJSScriptInstanceBase(p_script, p_owner),
        env_(p_env->get_ref()),
        class_id_(p_class_id) {}
};
#endif
