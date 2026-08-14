#include "jsb_script_instance.h"
#include "jsb_script.h"
#include "jsb_script_language.h"

static HashMap<const GDExtensionMethodInfo *, List<Variant>> default_value_cache_map;

struct ScriptInstanceInfo {
public:
	_FORCE_INLINE_ GDExtensionScriptInstanceInfo3 *operator&() { return &script_instance_info_; }

private:
	static GDExtensionBool set_func(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionConstVariantPtr p_value) {
		GodotJSScriptInstanceBase *script_instance = (GodotJSScriptInstanceBase *)p_instance;
		const StringName &name = *reinterpret_cast<const StringName *>(p_name);
		const Variant &value = *reinterpret_cast<const Variant *>(p_value);
		return (GDExtensionBool)script_instance->set(name, value);
	}

	static GDExtensionBool get_func(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionVariantPtr r_ret) {
		GodotJSScriptInstanceBase *script_instance = (GodotJSScriptInstanceBase *)p_instance;
		const StringName &name = *reinterpret_cast<const StringName *>(p_name);
		Variant &ret = *reinterpret_cast<Variant *>(r_ret);
		return script_instance->get(name, ret);
	}
	static const GDExtensionPropertyInfo *get_property_list_func(GDExtensionScriptInstanceDataPtr p_instance, uint32_t *r_count) {
		GodotJSScriptInstanceBase *script_instance = (GodotJSScriptInstanceBase *)p_instance;

		const LocalVector<PropertyInfo> &temporary_property_list = *script_instance->make_temporary_property_list();
		uint32_t count = temporary_property_list.size();

		GDExtensionPropertyInfo *arr = memnew_arr(GDExtensionPropertyInfo, count);
		for (uint32_t idx = 0; idx < count; idx++) {
			arr[idx] = ((const PropertyInfo &)(temporary_property_list[idx]))._to_gdextension();
		}

		*r_count = count;
		return arr;
	}

	static void free_property_list_func(GDExtensionScriptInstanceDataPtr p_instance, const GDExtensionPropertyInfo *p_list, uint32_t p_count) {
		memdelete_arr(p_list);
		GodotJSScriptInstanceBase *script_instance = (GodotJSScriptInstanceBase *)p_instance;
		script_instance->free_temporary_property_list();
	}

	static GDExtensionBool property_can_revert_func(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name) {
		const GodotJSScriptInstanceBase *script_instance = (GodotJSScriptInstanceBase *)p_instance;
		const StringName &name = *reinterpret_cast<const StringName *>(p_name);
		return (GDExtensionBool)script_instance->property_can_revert(name);
	}

	static GDExtensionBool property_get_revert_func(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionVariantPtr r_ret) {
		const GodotJSScriptInstanceBase *script_instance = (GodotJSScriptInstanceBase *)p_instance;
		const StringName &name = *reinterpret_cast<const StringName *>(p_name);
		Variant &ret = *reinterpret_cast<Variant *>(r_ret);
		return (GDExtensionBool)script_instance->property_get_revert(name, ret);
	}

	static GDExtensionObjectPtr get_owner_func(GDExtensionScriptInstanceDataPtr p_instance) {
		ScriptInstance *script_instance = (ScriptInstance *)p_instance;
		return (GDExtensionObjectPtr)script_instance->get_owner();
	}

	static const GDExtensionMethodInfo *get_method_list_func(GDExtensionScriptInstanceDataPtr p_instance, uint32_t *r_count) {
		GodotJSScriptInstanceBase *script_instance = (GodotJSScriptInstanceBase *)p_instance;
		const LocalVector<MethodInfo> &temporary_method_list = *script_instance->make_temporary_method_list();

		const uint32_t count = temporary_method_list.size();
		GDExtensionMethodInfo *arr = memnew_arr(GDExtensionMethodInfo, count);
		for (uint32_t idx = 0; idx < count; idx++) {
			MethodInfo minfo = temporary_method_list[idx];

			uint32_t argument_count = minfo.arguments.size();
			GDExtensionPropertyInfo *arguments{ nullptr };
			uint32_t default_argument_count = minfo.default_arguments.size();
			GDExtensionVariantPtr *default_arguments{ nullptr };

			if (argument_count) {
				arguments = memnew_arr(GDExtensionPropertyInfo, argument_count);
				for (uint32_t i = 0; i < argument_count; ++i) {
					arguments[i] = minfo.arguments[i]._to_gdextension();
				}
			}
			if (default_argument_count) {
				default_arguments = memnew_arr(GDExtensionVariantPtr, default_argument_count);
				for (uint32_t i = 0; i < default_argument_count; ++i) {
					default_arguments[i] = &minfo.default_arguments[i];
				}
			}

			arr[idx] = {
				.name = minfo.name._native_ptr(),
				.return_value{ minfo.return_val._to_gdextension() },
				.flags = minfo.flags,
				.id = minfo.id,
				.argument_count = argument_count,
				.arguments = arguments,
				.default_argument_count = default_argument_count,
				.default_arguments = default_arguments,
			};
		}

		*r_count = count;
		return arr;
	}
	static void free_method_list_func(GDExtensionScriptInstanceDataPtr p_instance, const GDExtensionMethodInfo *p_list, uint32_t p_count) {
		GodotJSScriptInstanceBase *script_instance = (GodotJSScriptInstanceBase *)p_instance;
		if (p_list->arguments) memdelete_arr(p_list->arguments);
		if (p_list->default_arguments) memdelete_arr(p_list->default_arguments);
		memdelete_arr(p_list);
		script_instance->free_temporary_method_list();
	}
	static GDExtensionVariantType get_property_type_func(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionBool *r_is_valid) {
		GodotJSScriptInstanceBase *script_instance = (GodotJSScriptInstanceBase *)p_instance;
		const StringName &name = *reinterpret_cast<const StringName *>(p_name);
		bool is_valid = false;
		Variant::Type type = script_instance->get_property_type(name, &is_valid);
		if (r_is_valid) *r_is_valid = (GDExtensionBool)is_valid;
		return (GDExtensionVariantType)type;
	}
	static GDExtensionBool validate_property_func(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionPropertyInfo *p_property) {
		GodotJSScriptInstanceBase *script_instance = (GodotJSScriptInstanceBase *)p_instance;
		PropertyInfo prop(p_property);
		script_instance->validate_property(prop);
		prop._update(p_property);
		return (GDExtensionBool) true;
	}
	static GDExtensionBool has_method_func(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name) {
		GodotJSScriptInstanceBase *script_instance = (GodotJSScriptInstanceBase *)p_instance;
		const StringName &name = *reinterpret_cast<const StringName *>(p_name);
		return (GDExtensionBool)script_instance->has_method(name);
	}
	static GDExtensionInt get_method_argument_count_func(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionBool *r_is_valid) {
		GodotJSScriptInstanceBase *script_instance = (GodotJSScriptInstanceBase *)p_instance;
		const StringName &name = *reinterpret_cast<const StringName *>(p_name);
		bool is_valid{ false };
		GDExtensionInt count = script_instance->get_method_argument_count(name, &is_valid);
		if (r_is_valid) *r_is_valid = (GDExtensionBool)is_valid;
		return count;
	}
	static void call_func(GDExtensionScriptInstanceDataPtr p_self, GDExtensionConstStringNamePtr p_method, const GDExtensionConstVariantPtr *p_args, GDExtensionInt p_argument_count, GDExtensionVariantPtr r_return, GDExtensionCallError *r_error) {
		GodotJSScriptInstanceBase *script_instance = (GodotJSScriptInstanceBase *)p_self;
		const StringName &method = *reinterpret_cast<const StringName *>(p_method);
		const Variant **args = (const Variant **)p_args;
		Variant &ret = *reinterpret_cast<Variant *>(r_return);
		*r_error = {}; // 进行 0 初始化，GDExtensionCallError 的字段没有定义初始值，所有局部 GDExtensionCallError 都需要初始化，防止出现垃圾值。
		ret = script_instance->callp(method, args, (int)p_argument_count, *r_error);
	}
	static void notification_func(GDExtensionScriptInstanceDataPtr p_instance, int32_t p_what, GDExtensionBool p_reversed) {
		GodotJSScriptInstanceBase *script_instance = (GodotJSScriptInstanceBase *)p_instance;
		script_instance->notification(p_what, (bool)p_reversed);
	}
	static void to_string_func(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionBool *r_is_valid, GDExtensionStringPtr r_out) {
		GodotJSScriptInstanceBase *script_instance = (GodotJSScriptInstanceBase *)p_instance;
		bool is_valid{ false };
		String str = script_instance->to_string(&is_valid);
		if (is_valid) *(String *)r_out = str;
		if (r_is_valid) *r_is_valid = (GDExtensionBool)is_valid;
	}
	static GDExtensionBool refcount_decremented_func(GDExtensionScriptInstanceDataPtr p_instance) {
		// 返回默认值 true，不阻止释放行为。
		return (GDExtensionBool) true;
	}
	static GDExtensionObjectPtr get_script_func(GDExtensionScriptInstanceDataPtr p_instance) {
		ScriptInstance *script_instance = (ScriptInstance *)p_instance;
		return (GDExtensionObjectPtr)script_instance->get_script()->_owner;
	}
	static GDExtensionBool is_placeholder_func(GDExtensionScriptInstanceDataPtr p_instance) {
		ScriptInstance *script_instance = (ScriptInstance *)p_instance;
		return script_instance->is_placeholder();
	}
	static GDExtensionScriptLanguagePtr get_language_func(GDExtensionScriptInstanceDataPtr p_instance) {
		GodotJSScriptInstanceBase *script_instance = (GodotJSScriptInstanceBase *)p_instance;
		return (GDExtensionScriptLanguagePtr)GodotJSScriptLanguage::get_singleton();
	}
	static void free_func(GDExtensionScriptInstanceDataPtr p_instance) {
		GodotJSScriptInstanceBase *script_instance = (GodotJSScriptInstanceBase *)p_instance;
		memdelete(script_instance);
	}

private:
	GDExtensionScriptInstanceInfo3 script_instance_info_{
		.set_func = &ScriptInstanceInfo::set_func,
		.get_func = &ScriptInstanceInfo::get_func,
		.get_property_list_func = &ScriptInstanceInfo::get_property_list_func,
		.free_property_list_func = &ScriptInstanceInfo::free_property_list_func,
		.get_class_category_func = nullptr,
		.property_can_revert_func = &ScriptInstanceInfo::property_can_revert_func,
		.property_get_revert_func = &ScriptInstanceInfo::property_get_revert_func,
		.get_owner_func = &ScriptInstanceInfo::get_owner_func,
		.get_property_state_func = nullptr, // 直接使用 godot 的 ScriptInstance::get_property_state 即可。
		.get_method_list_func = &ScriptInstanceInfo::get_method_list_func,
		.free_method_list_func = &ScriptInstanceInfo::free_method_list_func,
		.get_property_type_func = &ScriptInstanceInfo::get_property_type_func,
		.validate_property_func = &ScriptInstanceInfo::validate_property_func,
		.has_method_func = &ScriptInstanceInfo::has_method_func,
		.get_method_argument_count_func = &ScriptInstanceInfo::get_method_argument_count_func,
		.call_func = &ScriptInstanceInfo::call_func,
		.notification_func = &ScriptInstanceInfo::notification_func,
		.to_string_func = &ScriptInstanceInfo::to_string_func,
		.refcount_incremented_func = nullptr,
		.refcount_decremented_func = &ScriptInstanceInfo::refcount_decremented_func,
		.get_script_func = &ScriptInstanceInfo::get_script_func,
		.is_placeholder_func = &ScriptInstanceInfo::is_placeholder_func,
		.set_fallback_func = nullptr,
		.get_fallback_func = nullptr,
		.get_language_func = &ScriptInstanceInfo::get_language_func,
		.free_func = &ScriptInstanceInfo::free_func,
	};
};

namespace {
ScriptInstanceInfo script_instance_info;
}

ScriptInstance *ScriptInstance::get_script_instance(Object *p_object) {
#ifdef TOOLS_ENABLED
	if (PlaceholderScriptInstance *placeholder = PlaceholderScriptInstance::try_get_placeholder_script_instance(p_object)) {
		return placeholder;
	}
#endif // TOOLS_ENABLED

	void *obj_ptr{ nullptr };
	PtrToArg<Object *>::encode(p_object, &obj_ptr);
	return (GodotJSScriptInstanceBase *)godot::gdextension_interface::object_get_script_instance(obj_ptr, GodotJSScriptLanguage::get_singleton());
}

void ScriptInstance::set_script_instance(Object *p_object, ScriptInstance *p_instance) {
	void *obj_ptr{ nullptr };
	PtrToArg<Object *>::encode(p_object, &obj_ptr);
	godot::gdextension_interface::object_set_script_instance(obj_ptr, p_instance ? p_instance->extension_instance_ptr : nullptr);
}

ScriptInstance::ScriptInstance(const Ref<GodotJSScript> &p_script, Object *p_owner, GDExtensionScriptInstancePtr p_extension_instance_ptr) : script_(p_script), owner_(p_owner), extension_instance_ptr(p_extension_instance_ptr) {}

// =========== GodotJSScriptInstanceBase ==========
GodotJSScriptInstanceBase::ScriptCallProfilingScope::ScriptCallProfilingScope(const ScriptProfilingInfo &p_info, const StringName &p_method)
		: info_(p_info), method_(p_method) {
	start_time_ = Time::get_singleton()->get_ticks_usec();
}

GodotJSScriptInstanceBase::ScriptCallProfilingScope::~ScriptCallProfilingScope() {
	GodotJSScriptLanguage::get_singleton()->add_script_call_profile_info(
			info_.path_,
			info_.class_,
			method_,
			Time::get_singleton()->get_ticks_usec() - start_time_);
}

GodotJSScriptInstanceBase::GodotJSScriptInstanceBase(const Ref<GodotJSScript> &p_script, Object *p_owner) : ScriptInstance(p_script, p_owner, ::godot::gdextension_interface::script_instance_create3(&script_instance_info, this)) {}

GodotJSScriptInstanceBase::~GodotJSScriptInstanceBase() {
	jsb_check(owner_);

	// When script binding fails due to an invalid script, we delete the invalid script instance.
	if (script_.is_valid()) {
		script_->remove_script_instance_instance_owner(owner_);
	}
}

void GodotJSScriptInstanceBase::get_property_state(ScriptInstancePropertyState &p_state) const {
	const LocalVector<PropertyInfo> &p_list = *make_temporary_property_list();

	for (const PropertyInfo &pinfo : p_list) {
		if (pinfo.usage & PROPERTY_USAGE_STORAGE) {
			Pair<StringName, Variant> p;
			p.first = pinfo.name;
			if (get(p.first, p.second)) {
				p_state.push_back(p);
			}
		}
	}

	free_temporary_property_list();
}

namespace {
static PropertyInfo convert_property_info(const jsb::ScriptPropertyInfo &p_info) { return p_info; }
} // namespace
LocalVector<PropertyInfo> *GodotJSScriptInstanceBase::make_temporary_property_list() const {
	jsb_check(!temporary_script_property_list_cache);
	temporary_script_property_list_cache = memnew(LocalVector<PropertyInfo>);
	script_->get_script_property_list<PropertyInfo, &convert_property_info, LocalVector<PropertyInfo>>(*temporary_script_property_list_cache);
	return temporary_script_property_list_cache;
}

namespace {
static MethodInfo convert_method_info(const StringName &p_name, const jsb::ScriptMethodInfo &p_minfo) {
	MethodInfo ret(p_name);
	// TODO: 更多细节
	return ret;
}
} // namespace

LocalVector<MethodInfo> *GodotJSScriptInstanceBase::make_temporary_method_list() {
	jsb_check(!temporary_script_method_list_cache);
	temporary_script_method_list_cache = memnew(LocalVector<MethodInfo>);
	script_->get_script_method_list<MethodInfo, &convert_method_info, LocalVector<MethodInfo>>(*temporary_script_method_list_cache);
	return temporary_script_method_list_cache;
}

String GodotJSScriptInstanceBase::to_string(bool *r_valid) {
	if (r_valid) {
		*r_valid = false;
	}
	// TODO:
	return {}; //"<" + get_script()->_get_global_name() + "#" + itos(get_owner()->get_instance_id()) + ">" ;
}

#ifdef TOOLS_ENABLED
// ====== PlaceholderScriptInstance =====
HashMap<Object *, PlaceholderScriptInstance *> PlaceholderScriptInstance::placeholders_{};
PlaceholderScriptInstance::PlaceholderScriptInstance(const Ref<GodotJSScript> &p_script, Object *p_owner) : ScriptInstance(p_script, p_owner, ::godot::gdextension_interface::placeholder_script_instance_create(GodotJSScriptLanguage::get_singleton(), p_script->_owner, p_owner->_owner)) {
	placeholders_.insert(p_owner, this);
}

PlaceholderScriptInstance::~PlaceholderScriptInstance() {
	jsb_check(this->get_owner() != nullptr);
	placeholders_.erase(this->get_owner());
}

void PlaceholderScriptInstance::update(const TypedArray<Dictionary> &p_properties, const Dictionary &p_values) {
	::godot::gdextension_interface::placeholder_script_instance_update(extension_instance_ptr, &p_properties, &p_values);
}
#endif // TOOLS_ENABLED

// ====== GodotJSShadowScriptInstance =====
Variant::Type GodotJSShadowScriptInstance::get_property_type(const StringName &p_name, bool *r_is_valid) const {
	if (const jsb::ScriptPropertyInfo *ptr = script_->script_class_info_.properties.getptr(p_name)) {
		if (r_is_valid) *r_is_valid = true;
		return ptr->type;
	}
	return Variant::NIL;
}

bool GodotJSShadowScriptInstance::has_method(const StringName &p_method) const { return script_->script_class_info_.methods.has(p_method); }

const Variant GodotJSShadowScriptInstance::get_rpc_config() const { return script_->_get_rpc_config(); }
// ====== GodotJSScriptInstance =====
jsb::ScriptClassInfoPtr GodotJSScriptInstance::get_script_class() const {
	return env_ ? env_->get_script_class(class_id_) : nullptr;
}

void GodotJSScriptInstance::postbind() {
	jsb_check(env_);
	// Store initial value for cached props
	for (auto &it : this->script_->script_class_info_.properties) {
		if (it.value.cache) {
			Variant value;
			env_->get_script_property_value(object_id_, it.value, value);
			cache_property(it.key, value);
		}
	}
}

void GodotJSScriptInstance::cache_property(const StringName &name, const Variant &value) {
	property_cache_.insert(name, value);
}

bool GodotJSScriptInstance::set(const StringName &p_name, const Variant &p_value) {
	ERR_FAIL_NULL_V_MSG(env_, false, "ScriptInstance::set: env is null");
	GodotJSScript *sptr = script_.ptr();
	while (sptr) {
		if (const auto &it = sptr->script_class_info_.properties.find(p_name); it) {
			return env_->set_script_property_value(object_id_, it->value, p_value);
		}

		// TODO: Static variable?

		if (const auto &it = sptr->script_class_info_.methods.find(jsb_string_name(_set)); it) {
			Variant name = p_name;
			const Variant *args[2] = { &name, &p_value };

			GDExtensionCallError err{};
			Variant ret = env_->call_script_method(class_id_, object_id_, jsb_string_name(_set), (const Variant **)args, 2, err);
			if (err.error == GDEXTENSION_CALL_OK && ret.get_type() == Variant::BOOL && ret.operator bool()) {
				return true;
			}
		}

		sptr = sptr->base.ptr();
	}

	return false;
}

bool GodotJSScriptInstance::get(const StringName &p_name, Variant &r_ret) const {
	ERR_FAIL_NULL_V_MSG(env_, false, "ScriptInstance::get: env is null");
	const Variant *cached_value = property_cache_.getptr(p_name);

	if (cached_value) {
		r_ret = *cached_value;
		return true;
	}

	GodotJSScript *sptr = script_.ptr();
	while (sptr) {
		if (const auto &it = sptr->script_class_info_.properties.find(p_name); it) {
			return env_->get_script_property_value(object_id_, it->value, r_ret);
		}

		// TODO: constant?
		// TODO: static variable?
		// TODO: Inner class?

		if (const auto &it = sptr->script_class_info_.signals.find(p_name); it) {
			r_ret = Signal(owner_, p_name);
			return true;
		}

		if (const auto &it = sptr->script_class_info_.methods.find(p_name); it) {
			if (sptr->script_class_info_.rpc_config.has(p_name)) {
				// GDExtension: GDScriptRPCCallable not available, use regular Callable
				r_ret = Callable(owner_, p_name);
				return true;
			} else {
				if (!it->value.is_static()) {
					r_ret = Callable(owner_, p_name);
					return true;
				} else {
					// TODO: Warp static method to Callable
				}
			}
		}

		if (const auto &it = sptr->script_class_info_.methods.find(jsb_string_name(_get)); it) {
			Variant name = p_name;
			const Variant *args[1] = { &name };

			GDExtensionCallError err{};
			Variant ret = env_->call_script_method(class_id_, object_id_, jsb_string_name(_get), (const Variant **)args, 1, err);
			if (err.error == GDEXTENSION_CALL_OK && ret.get_type() != Variant::NIL) {
				r_ret = ret;
				return true;
			}
		}

		sptr = sptr->base.ptr();
	}

	return false;
}

LocalVector<PropertyInfo> *GodotJSScriptInstance::make_temporary_property_list() const {
	jsb_check(temporary_script_property_list_cache == nullptr);
	temporary_script_property_list_cache = memnew(LocalVector<PropertyInfo>);
	ERR_FAIL_NULL_V(env_, temporary_script_property_list_cache);

	GodotJSScript *sptr = script_.ptr();
	HashSet<StringName> properties;

	while (sptr) {
		if (const auto &it = sptr->script_class_info_.methods.find(jsb_string_name(_get_property_list)); it) {
			GDExtensionCallError err{};
			Variant ret = env_->call_script_method(class_id_, object_id_, jsb_string_name(_get_property_list), nullptr, 0, err);
			if (err.error == GDEXTENSION_CALL_OK && ret.get_type() != Variant::NIL) {
				ERR_FAIL_COND_V_MSG(ret.get_type() != Variant::ARRAY, temporary_script_property_list_cache, "Wrong type for _get_property_list, must be an array of dictionaries.");

				Array arr = ret;
				for (int i = 0; i < arr.size(); i++) {
					Dictionary d = arr[i];
					ERR_CONTINUE(!d.has("name"));
					ERR_CONTINUE(!d.has("type"));

					PropertyInfo pinfo = PropertyInfo::from_dict(d);

					ERR_CONTINUE(pinfo.name.is_empty() && (pinfo.usage & PROPERTY_USAGE_STORAGE));
					ERR_CONTINUE(pinfo.type < 0 || pinfo.type >= Variant::VARIANT_MAX);

					ERR_CONTINUE_MSG(properties.has(pinfo.name), vformat("Duplicate property \"%s\" in script: %s", pinfo.name, script_->get_path()));

					validate_property(pinfo);
					temporary_script_property_list_cache->push_back(pinfo);
					properties.insert(pinfo.name);
				}
			}
		}

		for (const auto &it : sptr->script_class_info_.properties) {
			ERR_CONTINUE_MSG(properties.has(it.value.name), vformat("Duplicate property \"%s\" in script: %s", it.value.name, script_->get_path()));
			PropertyInfo pinfo = (PropertyInfo)it.value;
			validate_property(pinfo);
			temporary_script_property_list_cache->push_back(pinfo);
		}

		sptr = sptr->base.ptr();
	}
	return temporary_script_property_list_cache;
}

const Variant GodotJSScriptInstance::get_rpc_config() const {
	return get_script_class()->rpc_config;
}

Variant::Type GodotJSScriptInstance::get_property_type(const StringName &p_name, bool *r_is_valid) const {
	GodotJSScript *sptr = script_.ptr();
	while (sptr) {
		if (const HashMap<StringName, jsb::ScriptPropertyInfo>::ConstIterator it = sptr->script_class_info_.properties.find(p_name)) {
			if (r_is_valid) *r_is_valid = true;
			return it->value.type;
		}

		sptr = sptr->base.ptr();
	}

	if (r_is_valid) *r_is_valid = false;
	return Variant::NIL;
}

void GodotJSScriptInstance::validate_property(PropertyInfo &p_property) const {
	ERR_FAIL_NULL_MSG(env_, "ScriptInstance::validate_property: env is null");
	GodotJSScript *sptr = script_.ptr();
	while (sptr) {
		if (const auto &it = sptr->script_class_info_.methods.find(jsb_string_name(_validate_property)); it) {
			Variant property = (Dictionary)p_property;
			const Variant *args[1] = { &property };

			GDExtensionCallError err{};
			Variant ret = env_->call_script_method(class_id_, object_id_, jsb_string_name(_validate_property), (const Variant **)args, 1, err);
			if (err.error == GDEXTENSION_CALL_OK && ret.get_type() != Variant::NIL) {
				p_property = PropertyInfo::from_dict(property);
			}
		}

		sptr = sptr->base.ptr();
	}
}

bool GodotJSScriptInstance::property_can_revert(const StringName &p_name) const {
	ERR_FAIL_NULL_V_MSG(env_, false, "ScriptInstance::property_can_revert: env is null");
	GodotJSScript *sptr = script_.ptr();
	while (sptr) {
		if (const auto &it = sptr->script_class_info_.methods.find(jsb_string_name(_property_can_revert)); it) {
			Variant name = p_name;
			const Variant *args[1] = { &name };

			GDExtensionCallError err{};
			Variant ret = env_->call_script_method(class_id_, object_id_, jsb_string_name(_property_can_revert), args, 1, err);
			if (err.error == GDEXTENSION_CALL_OK && ret.get_type() == Variant::BOOL && ret.operator bool()) {
				return true;
			}
		}

		sptr = sptr->base.ptr();
	}

	return false;
}

bool GodotJSScriptInstance::property_get_revert(const StringName &p_name, Variant &r_ret) const {
	ERR_FAIL_NULL_V_MSG(env_, false, "ScriptInstance::property_get_revert: env is null");
	GodotJSScript *sptr = script_.ptr();
	while (sptr) {
		if (const auto &it = sptr->script_class_info_.methods.find(jsb_string_name(_property_get_revert)); it) {
			Variant name = p_name;
			const Variant *args[1] = { &name };

			GDExtensionCallError err{};
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

bool GodotJSScriptInstance::has_method(const StringName &p_method) const {
	return script_->_has_method(p_method);
}

Variant GodotJSScriptInstance::callp(const StringName &p_method, const Variant **p_args, int p_argcount, GDExtensionCallError &r_error) {
	ERR_FAIL_NULL_V_MSG(env_, {}, "ScriptInstance::callp: env is null");
#if JSB_DEBUG
	if (profiling_info_.path_.is_empty()) {
		profiling_info_.path_ = script_->get_path();
		profiling_info_.class_ = get_script_class()->js_class_name;
	}
	ScriptCallProfilingScope profiling_scope(profiling_info_, p_method);
#endif
	return env_->call_script_method(class_id_, object_id_, p_method, p_args, p_argcount, r_error);
}

enum {
	NOTIFICATION_PREDELETE_CLEANUP = 3
};

void GodotJSScriptInstance::notification(int p_notification, bool p_reversed) {
	if (p_reversed && (p_notification == Object::NOTIFICATION_PREDELETE || p_notification == NOTIFICATION_PREDELETE_CLEANUP)) {
		// the JS counterpart is garbage collected (which finally caused Godot Object deleting)
		// so, some of the reversed notifications can not be handled by script instances
		return;
	}

	// Check if environment is being disposed to avoid calling into a destroyed JS context
	if (!env_ || env_->is_disposing()) {
		return;
	}

	// since `NOTIFICATION_READY` is not reversed, `notification` will be posted after `callp`.
	// so, we can't `call_prelude` here with `NOTIFICATION_READY`

	//TODO find the method named `_notification`, cal it with `p_notification` as `argv`
	//TODO call it at all type levels? @seealso `GDScriptInstance::notification`
	Variant value = p_notification;
	const Variant *argv[] = { &value };
	GDExtensionCallError error{};
	callp(jsb_string_name(_notification), argv, 1, error);
}
