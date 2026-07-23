#include "jsb_script_instance.h"
#include "jsb_script.h"
#include "jsb_script_language.h"


static HashMap<const GDExtensionMethodInfo*, List<Variant>> default_value_cache_map;


static GDExtensionMethodInfo method_info_to_gdextension(const MethodInfo &p_minfo, List<Variant> &r_default_value_caches) {
	/* Arguments: `default_arguments` is an array of size `argument_count`. */
	uint32_t argument_count = p_minfo.arguments.size();
	GDExtensionPropertyInfo *arguments{ nullptr };
	/* Default arguments: `default_arguments` is an array of size `default_argument_count`. */
	uint32_t default_argument_count = p_minfo.default_arguments.size();
	GDExtensionVariantPtr *default_arguments{ nullptr };

	if (argument_count) {
		arguments = memnew_arr(GDExtensionPropertyInfo, argument_count);
		for (uint32_t i = 0; i < argument_count; ++i) {
			arguments[i] = p_minfo.arguments[i]._to_gdextension();
		}
	}
	if (default_argument_count) {
		default_arguments = memnew_arr(GDExtensionVariantPtr, default_argument_count);
		for (uint32_t i = 0; i < default_argument_count; ++i) {
            r_default_value_caches.push_back(p_minfo.default_arguments[i]);
			default_arguments[i] = &r_default_value_caches.back()->get();
		}
	}

	GDExtensionMethodInfo ret{
		.name = p_minfo.name._native_ptr(),
		.return_value{ p_minfo.return_val._to_gdextension() },
		.flags= p_minfo.flags ,
		.id= p_minfo.id ,
		.argument_count= argument_count ,
		.arguments=arguments ,
		.default_argument_count= default_argument_count ,
		.default_arguments = default_arguments ,
	};
	return ret;
}

// static MethodInfo method_info_from_gdextension(const GDExtensionMethodInfo &pinfo) {
// 	MethodInfo ret(
// 			PropertyInfo(&pinfo.return_value),
// 			*reinterpret_cast<StringName *>(pinfo.name));
// 	ret.flags = pinfo.flags;
// 	ret.id = pinfo.id;

// 	for (uint32_t i = 0; i < pinfo.argument_count; i++) {
// 		ret.arguments.push_back(PropertyInfo(&pinfo.arguments[i]));
// 	}
// 	const Variant *def_values = (const Variant *)pinfo.default_arguments;
// 	for (uint32_t j = 0; j < pinfo.default_argument_count; j++) {
// 		ret.default_arguments.push_back(def_values[j]);
// 	}
// 	return ret;
// }

// 只有 GodotJSScriptInstanceBase 才用的上, PlaceholderScriptInstance 的 C 接口本身不携带 GDExtensionScriptInstanceInfo3。
// TODO: 如果有必要的话考虑不使用 godot::gdextension_interface::placeholder_script_instance_create() 来确保接口一致性
static GDExtensionScriptInstanceInfo3 script_instance_info {
    .set_func {[] (GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionConstVariantPtr p_value)-> GDExtensionBool {
        GodotJSScriptInstanceBase* script_instance = (GodotJSScriptInstanceBase*)p_instance;
        const StringName& name = *reinterpret_cast<const StringName *>(p_name);
        const Variant& value = *reinterpret_cast<const Variant *>(p_value);
        return (GDExtensionBool)script_instance->set(name, value);
    }},
    .get_func {[] (GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionVariantPtr r_ret) -> GDExtensionBool {
        GodotJSScriptInstanceBase* script_instance = (GodotJSScriptInstanceBase*)p_instance;
        const StringName& name = *reinterpret_cast<const StringName *>(p_name);
        Variant &ret = *reinterpret_cast<Variant *>(r_ret);
        return script_instance->get(name, ret);
    }},
    .get_property_list_func {[] (GDExtensionScriptInstanceDataPtr p_instance, uint32_t *r_count) -> const GDExtensionPropertyInfo* {
        GodotJSScriptInstanceBase* script_instance = (GodotJSScriptInstanceBase*)p_instance;

        LocalVector<GDExtensionPropertyInfo> properties;
        script_instance->get_property_list(&properties);
        uint32_t count = properties.size();

        *r_count = count;
        if (count == 0) return nullptr;

        GDExtensionPropertyInfo* arr = memnew_arr(GDExtensionPropertyInfo, count);
        memcpy((uint8_t*)arr, (uint8_t*)properties.ptr(), count * sizeof(GDExtensionPropertyInfo));
        return arr;
    }},
    .free_property_list_func {[] (GDExtensionScriptInstanceDataPtr p_instance, const GDExtensionPropertyInfo *p_list, uint32_t p_count) {
        memdelete_arr(p_list);
    }},
#ifdef TOOLS_ENABLED
    .get_class_category_func {[] (GDExtensionScriptInstanceDataPtr p_instance, GDExtensionPropertyInfo *p_class_category) -> GDExtensionBool{
        ScriptInstance* script_instance = (ScriptInstance*)p_instance;
        script_instance->get_script()->get_class_category()._update(p_class_category);
        return (GDExtensionBool)true;
    }},
#else // TOOLS_ENABLED
    .get_class_category_func = nullptr,
#endif // TOOLS_ENABLED
    .property_can_revert_func {[] (GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name) -> GDExtensionBool {
        const GodotJSScriptInstanceBase* script_instance = (GodotJSScriptInstanceBase*)p_instance;
        const StringName& name = *reinterpret_cast<const StringName *>(p_name);
        return (GDExtensionBool)script_instance->property_can_revert(name);
    }},
    .property_get_revert_func {[] (GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionVariantPtr r_ret) -> GDExtensionBool {
        const GodotJSScriptInstanceBase* script_instance = (GodotJSScriptInstanceBase*)p_instance;
        const StringName& name = *reinterpret_cast<const StringName *>(p_name);
        Variant &ret = *reinterpret_cast<Variant *>(r_ret);
        return (GDExtensionBool)script_instance->property_get_revert(name, ret);
    }},
    .get_owner_func {[] (GDExtensionScriptInstanceDataPtr p_instance) -> GDExtensionObjectPtr {
        ScriptInstance* script_instance = (ScriptInstance*)p_instance;
        return (GDExtensionObjectPtr)script_instance->get_owner();
    }},
    .get_property_state_func  = nullptr, // 直接使用 godot 的 ScriptInstance::get_property_state 即可。
    .get_method_list_func {[] (GDExtensionScriptInstanceDataPtr p_instance, uint32_t *r_count) -> const GDExtensionMethodInfo* {
        const GodotJSScriptInstanceBase* script_instance = (GodotJSScriptInstanceBase*)p_instance;

        LocalVector<GDExtensionMethodInfo> methods;
        List<Variant> default_value_cache;
        script_instance->get_method_list(&methods, default_value_cache);

        
        uint32_t count = methods.size();
        *r_count = count;
        
        GDExtensionMethodInfo* arr = memnew_arr(GDExtensionMethodInfo, count);
        memcpy((uint8_t*)arr, (uint8_t*)methods.ptr(), count * sizeof(GDExtensionMethodInfo));

        if (!default_value_cache.is_empty()) {
            default_value_cache_map[arr] = default_value_cache;
        }
        return arr;
    }},
    .free_method_list_func {[] (GDExtensionScriptInstanceDataPtr p_instance, const GDExtensionMethodInfo *p_list, uint32_t p_count) {
        if (default_value_cache_map.has(p_list)) default_value_cache_map.erase(p_list);
        if (p_list->arguments) memdelete_arr(p_list->arguments);
        if (p_list->default_arguments) memdelete_arr(p_list->default_arguments);
        memdelete_arr(p_list);
    }},
    .get_property_type_func {[] (GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionBool *r_is_valid) -> GDExtensionVariantType {
        GodotJSScriptInstanceBase* script_instance = (GodotJSScriptInstanceBase*)p_instance;
        const StringName& name = *reinterpret_cast<const StringName *>(p_name);
        bool is_valid = false;
        Variant::Type type = script_instance->get_property_type(name, &is_valid);
        if (r_is_valid) *r_is_valid = (GDExtensionBool)is_valid;
        return (GDExtensionVariantType)type;
    }},
    .validate_property_func {[] (GDExtensionScriptInstanceDataPtr p_instance, GDExtensionPropertyInfo *p_property) -> GDExtensionBool {
        GodotJSScriptInstanceBase* script_instance = (GodotJSScriptInstanceBase*)p_instance;
        PropertyInfo prop(p_property);
        script_instance->validate_property(prop);
        prop._update(p_property);
        return (GDExtensionBool)true;
    }},
    .has_method_func {[] (GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name) -> GDExtensionBool {
        GodotJSScriptInstanceBase* script_instance = (GodotJSScriptInstanceBase*)p_instance;
        const StringName& name = *reinterpret_cast<const StringName *>(p_name);
        return (GDExtensionBool)script_instance->has_method(name);
    }},
    .get_method_argument_count_func {[] (GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionBool *r_is_valid) -> GDExtensionInt {
        GodotJSScriptInstanceBase* script_instance = (GodotJSScriptInstanceBase*)p_instance;
        const StringName& name = *reinterpret_cast<const StringName *>(p_name);
        bool is_valid {false};
        GDExtensionInt count =  script_instance->get_method_argument_count(name, &is_valid);
        if (r_is_valid) *r_is_valid = (GDExtensionBool)is_valid;
        return count;
    }},
    .call_func {[] (GDExtensionScriptInstanceDataPtr p_self, GDExtensionConstStringNamePtr p_method, const GDExtensionConstVariantPtr *p_args, GDExtensionInt p_argument_count, GDExtensionVariantPtr r_return, GDExtensionCallError *r_error) {
        GodotJSScriptInstanceBase* script_instance = (GodotJSScriptInstanceBase*)p_self;
        const StringName& method = *reinterpret_cast<const StringName *>(p_method);
        const Variant** args = (const Variant**)p_args;
        Variant& ret = *reinterpret_cast<Variant*>(r_return);
        *r_error = {}; // 进行 0 初始化，GDExtensionCallError 的字段没有定义初始值，所有局部 GDExtensionCallError 都需要初始化，防止出现垃圾值。
        ret = script_instance->callp(method, args, (int)p_argument_count, *r_error);
    }},
    .notification_func {[] (GDExtensionScriptInstanceDataPtr p_instance, int32_t p_what, GDExtensionBool p_reversed) {
        GodotJSScriptInstanceBase* script_instance = (GodotJSScriptInstanceBase*)p_instance;
        script_instance->notification(p_what, (bool)p_reversed);
    }},
    .to_string_func {[] (GDExtensionScriptInstanceDataPtr p_instance, GDExtensionBool *r_is_valid, GDExtensionStringPtr r_out) {
        GodotJSScriptInstanceBase* script_instance = (GodotJSScriptInstanceBase*)p_instance;
        bool is_valid{false};
        String str = script_instance->to_string(&is_valid);
        if (is_valid) *(String*)r_out = str;
        if (r_is_valid) *r_is_valid = (GDExtensionBool)true;
    }},
    .refcount_incremented_func = nullptr, // TODO: 考虑将 Environment 中的引用计数管理挪到 GodotJSScriptInstanceBase
    .refcount_decremented_func = nullptr, // TODO: 考虑将 Environment 中的引用计数管理挪到 GodotJSScriptInstanceBase
    .get_script_func {[] (GDExtensionScriptInstanceDataPtr p_instance) -> GDExtensionObjectPtr {
        ScriptInstance* script_instance = (ScriptInstance*)p_instance;
        return (GDExtensionObjectPtr)script_instance->get_script().ptr();
    }},
    .is_placeholder_func {[] (GDExtensionScriptInstanceDataPtr p_instance) -> GDExtensionBool {
        ScriptInstance* script_instance = (ScriptInstance*)p_instance;
        return script_instance->is_placeholder();
    }},
    .set_fallback_func = nullptr,
    .get_fallback_func = nullptr,
    .get_language_func {[] (GDExtensionScriptInstanceDataPtr p_instance) -> GDExtensionScriptLanguagePtr {
        GodotJSScriptInstanceBase* script_instance = (GodotJSScriptInstanceBase*)p_instance;
        return (GDExtensionScriptLanguagePtr)GodotJSScriptLanguage::get_singleton();
    }},
    .free_func {[] (GDExtensionScriptInstanceDataPtr p_instance) {
        GodotJSScriptInstanceBase* script_instance = (GodotJSScriptInstanceBase*)p_instance;
        memdelete(script_instance);
    }},
};

ScriptInstance* ScriptInstance::get_script_instance(Object* p_object) {
    void *obj_ptr {nullptr};
    PtrToArg<Object*>::encode(p_object, &obj_ptr);
    return (GodotJSScriptInstanceBase *)godot::gdextension_interface::object_get_script_instance(obj_ptr, GodotJSScriptLanguage::get_singleton());
}

void ScriptInstance::set_script_instance(Object *p_object, ScriptInstance *p_instance)
{
    void *obj_ptr {nullptr};
    PtrToArg<Object*>::encode(p_object, &obj_ptr);
    godot::gdextension_interface::object_set_script_instance(obj_ptr, p_instance ? p_instance->extension_instance_ptr : nullptr);
}

ScriptInstance::ScriptInstance(const Ref<GodotJSScript> &p_script, Object* p_owner, GDExtensionScriptInstancePtr p_extension_instance_ptr):
    script_(p_script), owner_(p_owner), extension_instance_ptr(p_extension_instance_ptr) {}

// =========== GodotJSScriptInstanceBase ==========
GodotJSScriptInstanceBase::ScriptCallProfilingScope::ScriptCallProfilingScope(const ScriptProfilingInfo& p_info, const StringName& p_method)
            : info_(p_info), method_(p_method)
{
    start_time_ = Time::get_singleton()->get_ticks_usec();
}

GodotJSScriptInstanceBase::ScriptCallProfilingScope::~ScriptCallProfilingScope()
{
    GodotJSScriptLanguage::get_singleton()->add_script_call_profile_info(
        info_.path_,
        info_.class_,
        method_,
        Time::get_singleton()->get_ticks_usec() - start_time_);
}

GodotJSScriptInstanceBase::GodotJSScriptInstanceBase(const Ref<GodotJSScript> &p_script, Object* p_owner):
    ScriptInstance(p_script, p_owner, ::godot::gdextension_interface::script_instance_create3(&script_instance_info, this)) {}

GodotJSScriptInstanceBase::~GodotJSScriptInstanceBase()
{
    jsb_check(owner_);

    // When script binding fails due to an invalid script, we delete the invalid script instance.
    if (script_.is_valid())
    {
        jsb_check(script_->_get_language());
        const GodotJSScriptLanguage* lang = (GodotJSScriptLanguage*) script_->_get_language();
        std::lock_guard lock(lang->mutex_);
        script_->instances_.erase(owner_);
    }
}

template <typename T, typename = void>
struct has_reserve : std::false_type {};

template <typename T>
struct has_reserve<T, std::enable_if_t< std::is_invocable<decltype(&T::reserve), T*, int32_t>::value >> : std::true_type {};

template <typename PropertyStateTy>
static void reserve(PropertyStateTy &p_state, int32_t p_capacity) {
    if constexpr(has_reserve<PropertyStateTy>::value) {
        p_state.reserve(p_capacity);
    }
}

void GodotJSScriptInstanceBase::get_property_state(ScriptInstancePropertyState &p_state) const
{
    bool sss = has_reserve<List<int>>::value;
    bool ccc = has_reserve<LocalVector<int>>::value;
    LocalVector<GDExtensionPropertyInfo> p_properties;
	get_property_list(&p_properties);
    reserve(p_properties, p_properties.size());
	for (const GDExtensionPropertyInfo &E : p_properties) {
		if (E.usage & PROPERTY_USAGE_STORAGE) {
			Pair<StringName, Variant> p;
			p.first = *(StringName*)E.name;
			if (get(p.first, p.second)) {
				p_state.push_back(p);
			}
		}
	}
}

#ifdef TOOLS_ENABLED
// ====== PlaceholderScriptInstance =====
PlaceholderScriptInstance::PlaceholderScriptInstance(const Ref<GodotJSScript> &p_script, Object* p_owner):
ScriptInstance(p_script, p_owner,::godot::gdextension_interface::placeholder_script_instance_create(GodotJSScriptLanguage::get_singleton(), p_script->_owner, p_owner->_owner))
{
}

void PlaceholderScriptInstance::update(const TypedArray<Dictionary> &p_properties, const Dictionary &p_values)
{
    ::godot::gdextension_interface::placeholder_script_instance_update(extension_instance_ptr, &p_properties, &p_values);
}
#endif // TOOLS_ENABLED

// ====== GodotJSShadowScriptInstance =====
void GodotJSShadowScriptInstance::get_property_list(LocalVector<GDExtensionPropertyInfo>* p_properties) const
{
    TypedArray<Dictionary> property_list = script_->_get_script_property_list();
    for (const Dictionary& pinfo : property_list)
    {
        p_properties->push_back(PropertyInfo::from_dict(pinfo)._to_gdextension());
    }
}
Variant::Type GodotJSShadowScriptInstance::get_property_type(const StringName& p_name, bool* r_is_valid) const
{
    if (const jsb::ScriptPropertyInfo* ptr = script_->script_class_info_.properties.getptr(p_name))
    {
        if (r_is_valid) *r_is_valid = true;
        return ptr->type;
    }
    return Variant::NIL;
}

void GodotJSShadowScriptInstance::get_method_list(LocalVector<GDExtensionMethodInfo>* p_list, List<Variant> &r_default_value_caches) const
{
    TypedArray<Dictionary> method_list = script_->_get_script_method_list();
    for (const Dictionary& minfo : method_list)
    {
        p_list->push_back(method_info_to_gdextension(MethodInfo::from_dict(minfo), r_default_value_caches));
    }
}
bool GodotJSShadowScriptInstance::has_method(const StringName& p_method) const { return script_->script_class_info_.methods.has(p_method); }

const Variant GodotJSShadowScriptInstance::get_rpc_config() const { return script_->_get_rpc_config(); }
// ====== GodotJSScriptInstance =====
jsb::ScriptClassInfoPtr GodotJSScriptInstance::get_script_class() const
{
    return env_->get_script_class(class_id_);
}

void GodotJSScriptInstance::postbind()
{
    // Store initial value for cached props
    for (auto& it : this->script_->script_class_info_.properties)
    {
        if (it.value.cache)
        {
            Variant value;
            env_->get_script_property_value(object_id_, it.value, value);
            cache_property(it.key, value);
        }
    }
}

void GodotJSScriptInstance::cache_property(const StringName& name, const Variant& value)
{
    property_cache_.insert(name, value);
}

bool GodotJSScriptInstance::set(const StringName& p_name, const Variant& p_value)
{
    GodotJSScript* sptr = script_.ptr();
    while (sptr)
    {
        if (const auto& it = sptr->script_class_info_.properties.find(p_name); it)
        {
            return env_->set_script_property_value(object_id_, it->value, p_value);
        }

        // TODO: Static variable?

        if (const auto& it = sptr->script_class_info_.methods.find(jsb_string_name(_set)); it)
        {
            Variant name = p_name;
            const Variant *args[2] = { &name, &p_value };

            GDExtensionCallError err {};
            Variant ret = env_->call_script_method(class_id_, object_id_, jsb_string_name(_set), (const Variant **)args, 2, err);
            if (err.error == GDEXTENSION_CALL_OK && ret.get_type() == Variant::BOOL && ret.operator bool()) {
                return true;
            }
        }

        sptr = sptr->base.ptr();
    }

    return false;
}

bool GodotJSScriptInstance::get(const StringName& p_name, Variant& r_ret) const
{
    const Variant* cached_value = property_cache_.getptr(p_name);

    if (cached_value)
    {
        r_ret = *cached_value;
        return true;
    }

    GodotJSScript* sptr = script_.ptr();
    while (sptr)
    {
        if (const auto& it = sptr->script_class_info_.properties.find(p_name); it)
        {
            return env_->get_script_property_value(object_id_, it->value, r_ret);
        }

        // TODO: constant?
        // TODO: static variable?
        // TODO: Inner class?

        if (const auto& it = sptr->script_class_info_.signals.find(p_name); it)
        {
            r_ret =  Signal(owner_, p_name);
            return true;
        }

        if (const auto& it = sptr->script_class_info_.methods.find(p_name); it)
        {
            if (sptr->script_class_info_.rpc_config.has(p_name)) {
                // GDExtension: GDScriptRPCCallable not available, use regular Callable
                r_ret = Callable(owner_, p_name);
                return true;
            } else {
                if (!it->value.is_static())
                {
                    r_ret = Callable(owner_, p_name);
                    return true;
                } else {
                    // TODO: Warp static method to Callable
                }
            }
        }

        if (const auto& it = sptr->script_class_info_.methods.find(jsb_string_name(_get)); it)
        {
            Variant name = p_name;
            const Variant *args[1] = { &name };

            GDExtensionCallError err {};
            Variant ret = env_->call_script_method(class_id_, object_id_, jsb_string_name(_get), (const Variant **)args, 1, err);
            if (err.error == GDEXTENSION_CALL_OK && ret.get_type() != Variant::NIL)
            {
                r_ret = ret;
                return true;
            }
        }

        sptr = sptr->base.ptr();
    }

    return false;
}

void GodotJSScriptInstance::get_property_list(LocalVector<GDExtensionPropertyInfo>* p_properties) const
{
    GodotJSScript* sptr = script_.ptr();
    HashSet<String> properties;

    while (sptr)
    {
        if (const auto& it = sptr->script_class_info_.methods.find(jsb_string_name(_get_property_list)); it)
        {
            GDExtensionCallError err {};
            Variant ret = env_->call_script_method(class_id_, object_id_, jsb_string_name(_get_property_list), nullptr, 0, err);
            if (err.error == GDEXTENSION_CALL_OK && ret.get_type() != Variant::NIL)
            {
                ERR_FAIL_COND_MSG(ret.get_type() != Variant::ARRAY, "Wrong type for _get_property_list, must be an array of dictionaries.");

                Array arr = ret;
                for (int i = 0; i < arr.size(); i++)
                {
                    Dictionary d = arr[i];
                    ERR_CONTINUE(!d.has("name"));
                    ERR_CONTINUE(!d.has("type"));

                    PropertyInfo pinfo = PropertyInfo::from_dict(d);

                    ERR_CONTINUE(pinfo.name.is_empty() && (pinfo.usage & PROPERTY_USAGE_STORAGE));
                    ERR_CONTINUE(pinfo.type < 0 || pinfo.type >= Variant::VARIANT_MAX);

                    ERR_CONTINUE_MSG(properties.has(pinfo.name), vformat("Duplicate property \"%s\" in script: %s", pinfo.name, script_->get_path()));

                    validate_property(pinfo);
                    p_properties->push_back(pinfo._to_gdextension());
                    properties.insert(pinfo.name);
                }
            }
        }

#ifdef TOOLS_ENABLED
        p_properties->push_back(sptr->get_class_category()._to_gdextension());
#endif
        for (const auto& it : sptr->script_class_info_.properties)
        {
            ERR_CONTINUE_MSG(properties.has(it.value.name), vformat("Duplicate property \"%s\" in script: %s", it.value.name, script_->get_path()));
            PropertyInfo pinfo = (PropertyInfo)it.value;
            validate_property(pinfo);
            p_properties->push_back(pinfo._to_gdextension());
        }

        sptr = sptr->base.ptr();
    }
}

const Variant GodotJSScriptInstance::get_rpc_config() const
{
    return get_script_class()->rpc_config;
}

Variant::Type GodotJSScriptInstance::get_property_type(const StringName& p_name, bool* r_is_valid) const
{
    GodotJSScript* sptr = script_.ptr();
    while (sptr) {
        if (const HashMap<StringName, jsb::ScriptPropertyInfo>::ConstIterator it = sptr->script_class_info_.properties.find(p_name))
        {
            if (r_is_valid) *r_is_valid = true;
            return it->value.type;
        }

        sptr = sptr->base.ptr();
    }

    if (r_is_valid) *r_is_valid = false;
    return Variant::NIL;
}

void GodotJSScriptInstance::validate_property(PropertyInfo& p_property) const
{
    GodotJSScript* sptr = script_.ptr();
    while(sptr)
    {
        if (const auto& it = sptr->script_class_info_.methods.find(jsb_string_name(_validate_property)); it)
        {
            Variant property = (Dictionary)p_property;
            const Variant *args[1] = { &property };

            GDExtensionCallError err {};
            Variant ret = env_->call_script_method(class_id_, object_id_, jsb_string_name(_validate_property), (const Variant **)args, 1, err);
            if (err.error == GDEXTENSION_CALL_OK && ret.get_type() != Variant::NIL)
            {
                p_property =  PropertyInfo::from_dict(property);
            }
        }

        sptr = sptr->base.ptr();
    }
}

bool GodotJSScriptInstance::property_can_revert(const StringName& p_name) const
{
    GodotJSScript* sptr = script_.ptr();
    while(sptr)
    {
        if (const auto& it = sptr->script_class_info_.methods.find(jsb_string_name(_property_can_revert)); it)
        {
            Variant name = p_name;
            const Variant *args[1] = { &name };

            GDExtensionCallError err {};
            Variant ret = env_->call_script_method(class_id_, object_id_, jsb_string_name(_property_can_revert), args, 1, err);
            if (err.error == GDEXTENSION_CALL_OK && ret.get_type() == Variant::BOOL && ret.operator bool()) {
                return true;
            }
        }

        sptr = sptr->base.ptr();
    }

	return false;
}

bool GodotJSScriptInstance::property_get_revert(const StringName& p_name, Variant& r_ret) const
{
    GodotJSScript* sptr = script_.ptr();
    while(sptr)
    {
        if (const auto& it = sptr->script_class_info_.methods.find(jsb_string_name(_property_get_revert)); it)
        {
            Variant name = p_name;
            const Variant *args[1] = { &name };

            GDExtensionCallError err {};
            Variant ret = env_->call_script_method(class_id_, object_id_, jsb_string_name(_property_get_revert), args, 1, err);
            if (err.error == GDEXTENSION_CALL_OK && ret.get_type() == Variant::BOOL && ret.operator bool()) {
                r_ret = ret;
                return true;
            }
        }

        sptr = sptr->base.ptr();
    }

	return false;
}

void GodotJSScriptInstance::get_method_list(LocalVector<GDExtensionMethodInfo>* p_list, List<Variant> &r_default_value_caches) const
{
    TypedArray<Dictionary> method_list = script_->_get_script_method_list();
    for (const Dictionary &minfo_dict: method_list)
    {
        p_list->push_back(method_info_to_gdextension(MethodInfo::from_dict(minfo_dict), r_default_value_caches));
    }
}

bool GodotJSScriptInstance::has_method(const StringName& p_method) const
{
    return script_->_has_method(p_method);
}

Variant GodotJSScriptInstance::callp(const StringName& p_method, const Variant** p_args, int p_argcount, GDExtensionCallError& r_error)
{
#if JSB_DEBUG
    if (profiling_info_.path_.is_empty())
    {
        profiling_info_.path_ = script_->get_path();
        profiling_info_.class_ = get_script_class()->js_class_name;
    }
    ScriptCallProfilingScope profiling_scope(profiling_info_, p_method);
#endif
    return env_->call_script_method(class_id_, object_id_, p_method, p_args, p_argcount, r_error);
}

void GodotJSScriptInstance::notification(int p_notification, bool p_reversed)
{
    if (p_reversed &&
        (p_notification == Object::NOTIFICATION_PREDELETE)) // NOTIFICATION_PREDELETE_CLEANUP
    {
        // the JS counterpart is garbage collected (which finally caused Godot Object deleting)
        // so, some of the reversed notifications can not be handled by script instances
        return;
    }

    // Check if environment is being disposed to avoid calling into a destroyed JS context
    if (!env_ || env_->is_disposing())
    {
        return;
    }

    // since `NOTIFICATION_READY` is not reversed, `notification` will be posted after `callp`.
    // so, we can't `call_prelude` here with `NOTIFICATION_READY`

    //TODO find the method named `_notification`, cal it with `p_notification` as `argv`
    //TODO call it at all type levels? @seealso `GDScriptInstance::notification`
    Variant value = p_notification;
    const Variant* argv[] = {&value};
    GDExtensionCallError error;
    callp(jsb_string_name(_notification), argv, 1, error);
}
