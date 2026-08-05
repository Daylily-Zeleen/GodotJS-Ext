#include "jsb_script.h"
#include "../internal/jsb_path_util.h"
#include "jsb_script_instance.h"
#include "jsb_script_language.h"

#include <compat/misc.h>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/resource_loader.hpp>

GodotJSScript::GodotJSScript() : script_list_(this)
#ifdef TOOLS_ENABLED
							   , class_category_{ Variant::NIL, StringName(), PROPERTY_HINT_NONE, "", PROPERTY_USAGE_CATEGORY }
#endif // TOOLS_ENABLED
{
	{
		JSB_BENCHMARK_SCOPE(GodotJSScript, Construct);
		std::lock_guard lock(GodotJSScriptLanguage::get_singleton()->mutex_);
		GodotJSScriptLanguage::get_singleton()->script_list_.add(&script_list_);
	}
	JSB_LOG(VeryVerbose, "new GodotJSScript addr:%d", (uintptr_t)this);
}

GodotJSScript::~GodotJSScript() {
	JSB_LOG(VeryVerbose, "delete GodotJSScript addr:%d", (uintptr_t)this);

	{
		JSB_BENCHMARK_SCOPE(GodotJSScript, Destruct);
		std::lock_guard lock(GodotJSScriptLanguage::get_singleton()->mutex_);

		script_list_.remove_from_list();
	}
}

bool GodotJSScript::_can_instantiate() const {
#ifdef TOOLS_ENABLED
	return _is_valid() && !script_class_info_.is_abstract() && (script_class_info_.is_tool() || !Engine::get_singleton()->is_editor_hint());
#else
	return _is_valid() && !script_class_info_.is_abstract();
#endif
}

void GodotJSScript::_set_source_code(const String &p_code) {
	if (source_ == p_code) return;

	source_ = p_code;
#ifdef TOOLS_ENABLED
	source_changed_cache = true;
#endif
}

Ref<Script> GodotJSScript::_get_base_script() const {
	ensure_module_loaded();
	//jsb_notice(loaded_, "script not loaded");

	// return the base script in order to traverse methods/properties from inheritance hierarchy
	return base;
}

StringName GodotJSScript::_get_global_name() const {
	ensure_module_loaded();
	return _is_valid() ? script_class_info_.js_class_name : StringName();
}

bool GodotJSScript::_inherits_script(const Ref<Script> &p_script) const {
	jsb_check(loaded_);

	// check if the current script inherits from `p_script`
	//TODO `inherits_script` seems to be called only by Array::assign, it's enough for now without an implementation.
	//TODO iterate the prototype chain, check if the current script inherits from `p_script`

	return false;
}

// this method is called in `EditorStandardSyntaxHighlighter::_update_cache()` without checking `script->is_valid()`
StringName GodotJSScript::_get_instance_base_type() const {
	ensure_module_loaded();
	return _is_valid() ? script_class_info_.native_class_name : StringName();
}

ScriptInstance *GodotJSScript::instance_and_native_object_create(const v8::Local<v8::Object> &p_this, bool p_is_temp_allowed) {
	ensure_module_loaded();
	if (jsb_unlikely(!loaded_ || !is_valid_internal())) {
		JSB_LOG(Error, "cannot instantiate native object for invalid script: %s", get_path());
		return nullptr;
	}
	jsb_check(is_valid_internal());
	jsb_check(loaded_);

	// godot 暴露的 ClassDB 绑定，该接口返回 Variant, 如果是 RefCounted 则会自行处理引用
	const Variant var = ClassDB::instantiate(script_class_info_.native_class_name);
	Object *owner = var;

	ScriptInstance *instance = instance_create(p_this, owner, p_is_temp_allowed);
	if (!instance && !owner->is_class(RefCounted::get_class_static())) {
		memdelete(owner);
	}
	return instance;
}

void GodotJSScript::remove_script_instance_instance_owner(Object *p_owner) {
	jsb_check(GodotJSScriptLanguage::get_singleton());
	std::lock_guard lock(GodotJSScriptLanguage::get_singleton()->mutex_);
	instances_.erase(p_owner);
}

GodotJSScriptInstance *GodotJSScript::try_create_script_instance(Object *p_owner, jsb::JSEnvironment &p_env, jsb::ScriptClassID p_script_class_id, auto p_bind_and_get_native_object_id) {
	/* STEP 1, CREATE */
	GodotJSScriptInstance *instance = memnew(GodotJSScriptInstance(
			Ref(this),
			p_owner,
			p_env,
			p_script_class_id));
	ScriptInstance::set_script_instance(instance->get_owner(), instance);

	/* STEP 2, INITIALIZE AND CONSTRUCT */
	{
		std::lock_guard lock(GodotJSScriptLanguage::get_singleton()->mutex_);
		instances_.insert(p_owner);
	}
	instance->object_id_ = p_bind_and_get_native_object_id();
	if (!instance->object_id_) {
		instance->script_ = Ref<GodotJSScript>();
		ScriptInstance::set_script_instance(instance->get_owner(), nullptr);
		//NOTE `instance` becomes an invalid pointer since it's deleted in `set_script_instance`
		remove_script_instance_instance_owner(p_owner);
		JSB_LOG(Error, "Error constructing a GodotJSScriptInstance for %s (%s)", script_class_info_.js_class_name, script_class_info_.module_id);
		return nullptr;
	}

	return instance;
}

ScriptInstance *GodotJSScript::instance_create(const v8::Local<v8::Object> &p_this, Object *p_owner, bool p_is_temp_allowed) {
	ensure_module_loaded();
	if (jsb_unlikely(!loaded_ || !is_valid_internal())) {
		JSB_LOG(Error, "cannot create script instance from invalid script: %s", get_path());
		return nullptr;
	}
	jsb_check(is_valid_internal());
	jsb_check(loaded_);

	jsb::JSEnvironment env(get_path(), p_is_temp_allowed);
	jsb::JavaScriptModule *module = nullptr;
	const Error err = env->load(script_class_info_.module_id, &module);
	// jsb_ensure(!env->is_shadow()); // TODO: 待确认
	jsb_ensuref(module && err == OK, "JS Module not found: %s", script_class_info_.module_id);
	const jsb::NativeClassID native_class_id = env->get_script_class(module->script_class_id)->native_class_id;

	return try_create_script_instance(
			p_owner, env, module->script_class_id, [&env, native_class_id, p_owner, p_this] { return env->bind_godot_object(native_class_id, p_owner, p_this); });
}

ScriptInstance *GodotJSScript::instance_construct(Object *p_this, bool p_is_temp_allowed, const Variant **p_args, int p_argcount) {
	ensure_module_loaded();
	if (jsb_unlikely(!loaded_ || !is_valid_internal())) {
		JSB_LOG(Error, "cannot construct script instance from invalid script: %s", get_path());
		return nullptr;
	}
	jsb_check(is_valid_internal());
	jsb_check(loaded_);
	JSB_LOG(Verbose, "create instance %d of %s(%s)", (uintptr_t)p_this, script_class_info_.native_class_name, script_class_info_.module_id);

	if (!ClassDB::is_parent_class(p_this->get_class(), script_class_info_.native_class_name)) {
		JSB_LOG(Error, "GodotJS class %s (%s) cannot be instantiated for a %s, it requires a %s", script_class_info_.js_class_name, script_class_info_.module_id, p_this->get_class(), script_class_info_.native_class_name);
		return nullptr;
	}

	jsb::JSEnvironment env(get_path(), p_is_temp_allowed);
	if (env.is_shadow()) {
		ScriptInstance *shadow_instance = memnew(GodotJSShadowScriptInstance(Ref(this), p_this));

		// ensure `GodotJSScript::instance_has(obj)` works properly even if a shadow instance is used.
		{
			std::lock_guard lock(GodotJSScriptLanguage::get_singleton()->mutex_);
			instances_.insert(shadow_instance->get_owner());
		}
		return shadow_instance;
	}

	jsb::JavaScriptModule *module = nullptr;
	const Error err = env->load(script_class_info_.module_id, &module);
	jsb_ensuref(module && err == OK, "JS Module not found: %s", script_class_info_.module_id);
	const jsb::ScriptClassID script_class_id = module->script_class_id;

	GodotJSScriptInstance *instance = try_create_script_instance(
			p_this, env, module->script_class_id, [&env, p_this, script_class_id, p_args, p_argcount] { return env->crossbind(p_this, script_class_id, p_args, p_argcount); });

	if (instance) {
		instance->postbind();
		jsb_ensure(env->verify_object(p_this));
	}

	return instance;
}

Error GodotJSScript::_reload(bool p_keep_state) {
	if (!loaded_) return OK; // TODO: 这里堵死了怎么 reload ?
	if (!is_valid_internal()) return ERR_UNAVAILABLE;

	if (!p_keep_state) {
		std::lock_guard lock(GodotJSScriptLanguage::get_singleton()->mutex_);
		if (instances_.size()) {
			return ERR_ALREADY_IN_USE;
		}
	}

	if (!p_keep_state) {
		//TODO discard the object and crossbind again, but for now we just reload it normally
	}

	// (common situation) preserve the object and change its prototype
	const StringName &module_id = script_class_info_.module_id;
	jsb::JSEnvironment env(get_path(), true);

	//TODO different env has different module state, we need to refresh the state in all envs when marking a module as dirty somewhere
	const jsb::ModuleReloadResult::Type result = env->mark_as_reloading(module_id);
	if (result == jsb::ModuleReloadResult::Requested) {
		//TODO `Callable` objects bound with this script should be invalidated somehow?
		// ...

		loaded_ = false;
	} else if (result != jsb::ModuleReloadResult::NoChanges) {
		JSB_LOG(Warning, "failed to mark module as reloading: %s (%d)", module_id, result);
	}

	return OK;
}

#ifdef TOOLS_ENABLED
StringName GodotJSScript::_get_doc_class_name() const {
	//TODO not verified
	TypedArray<Dictionary> docs = _get_documentation();
	if (!docs.is_empty()) return docs[0].operator Dictionary()["name"];
	return {};
}

TypedArray<Dictionary> GodotJSScript::_get_documentation() const {
	ensure_module_loaded();
	if (!loaded_ || !is_valid_internal()) return {};

	const Dictionary class_basic_info = GodotJSScriptLanguage::get_singleton()->_get_global_class_name(get_path());
	String base_type = class_basic_info["base_type"];
	Dictionary class_doc;
	class_doc["name"] = class_basic_info["class"];
	class_doc["inherits"] = base_type.is_empty() ? Variant("Object") : Variant(base_type);
	class_doc["is_script_doc"] = true;
	class_doc["brief_description"] = script_class_info_.doc.brief_description;
	class_doc["is_deprecated"] = script_class_info_.doc.is_deprecated;
	class_doc["is_experimental"] = script_class_info_.doc.is_experimental;
	class_doc["deprecated_message"] = script_class_info_.doc.deprecated_message;
	class_doc["experimental_message"] = script_class_info_.doc.experimental_message;
	class_doc["script_path"] = get_path();

	TypedArray<Dictionary> properties;
	for (const auto &item : script_class_info_.properties) {
		Dictionary prop_doc;
		prop_doc["name"] = item.key;
		prop_doc["description"] = item.value.doc.brief_description;
		prop_doc["is_deprecated"] = item.value.doc.is_deprecated;
		prop_doc["is_experimental"] = item.value.doc.is_experimental;
		prop_doc["deprecated_message"] = item.value.doc.deprecated_message;
		prop_doc["experimental_message"] = item.value.doc.experimental_message;
		properties.push_back(prop_doc);
	}
	class_doc["properties"] = properties;

	TypedArray<Dictionary> methods;
	for (const auto &item : script_class_info_.methods) {
		Dictionary method_doc;
		method_doc["name"] = item.key;
		// TODO: 填充完整函数文档
		methods.push_back(method_doc);
	}
	class_doc["methods"] = methods;

	TypedArray<Dictionary> docs;
	docs.push_back(class_doc);
	return docs;
}

String GodotJSScript::_get_class_icon_path() const {
	ensure_module_loaded();
	jsb_check(loaded_);
	return script_class_info_.icon;
}

const jsb::ScriptPropertyInfo &GodotJSScript::get_class_category() const {
	ensure_module_loaded();
	jsb_check(loaded_);

	String path = get_path();
	String scr_name;

	if (is_built_in()) {
		if (get_name().is_empty()) {
			scr_name = TTR("Built-in script");
		} else {
			scr_name = vformat("%s (%s)", get_name(), TTR("Built-in"));
		}
	} else {
		if (get_name().is_empty()) {
			scr_name = path.get_file();
		} else {
			scr_name = get_name();
		}
	}

	const_cast<GodotJSScript *>(this)->class_category_.name = scr_name;
	const_cast<GodotJSScript *>(this)->class_category_.hint_string = path;
	return class_category_;
}
#endif

bool GodotJSScript::_has_method(const StringName &p_method) const {
	ensure_module_loaded();
	jsb_check(loaded_);

	String exposed_name = p_method;

	if (exposed_name.begins_with("_")) {
		exposed_name = jsb::internal::NamingUtil::get_member_name(exposed_name);
	}

	const GodotJSScript *current = this;
	while (current) {
		//TODO temp fix
		if (!current->loaded_) const_cast<GodotJSScript *>(current)->load_module_immediately();
		if (current->_is_valid() && current->script_class_info_.methods.has(exposed_name)) return true;
		current = current->base.ptr();
	}

	// ensure `_ready` called even if it's not actually defined in scripts
	if (p_method == jsb_string_name(_ready)) {
		// only a `Node` class has `_ready` call
		if (ClassDB::is_parent_class(get_instance_base_type(), jsb_string_name(Node))) {
			return true;
		}
	}
	return false;
}

Dictionary GodotJSScript::_get_method_info(const StringName &p_method) const {
	jsb_check(loaded_);
	jsb_check(_has_method(p_method));
	//TODO details?
	Dictionary item;
	item["name"] = p_method;
	return item;
}

ScriptLanguage *GodotJSScript::_get_language() const {
	return GodotJSScriptLanguage::get_singleton();
}

bool GodotJSScript::_has_script_signal(const StringName &p_signal) const {
	if (_is_valid()) {
		if (script_class_info_.signals.has(p_signal)) {
			return true;
		}

		if (base.is_valid()) {
			return base->has_script_signal(p_signal);
		}
	}

	return false;
}

Variant GodotJSScript::_get_script_method_argument_count(const StringName &p_method) const {
	return {}; // JS 函数本身不定参数（TODO: 有没有办法解析出定义的参数个数？）
}

bool GodotJSScript::_has_property_default_value(const StringName &p_property) const {
	ensure_module_loaded();
	if (const HashMap<StringName, Variant>::ConstIterator it = member_default_values_cache.find(p_property)) {
		return true;
	}

	if (base.is_valid() && base->_is_valid()) {
		return base->_has_property_default_value(p_property);
	}
	return false;
}

Variant GodotJSScript::_get_property_default_value(const StringName &p_property) const {
	ensure_module_loaded();
	if (const HashMap<StringName, Variant>::ConstIterator it = member_default_values_cache.find(p_property)) {
		return it->value;
	}

	if (base.is_valid() && base->_is_valid()) {
		return base->_get_property_default_value(p_property);
	}
	return Variant();
}

Variant GodotJSScript::_get_rpc_config() const {
	ensure_module_loaded();
	jsb_check(loaded_);

	return script_class_info_.rpc_config; // TODO: 是否需要包含父类？
}

bool GodotJSScript::_has_static_method(const StringName &p_method) const {
	ensure_module_loaded();
	if (!_is_valid()) return false;
	// TODO: 当前 ScriptInfo 中似乎不包含静态函数信息
	return false; // script_class_info_.methods.has(p_method);
}

Error GodotJSScript::load_source_code(const String &p_path) {
#ifdef TOOLS_ENABLED
	const String source_code = FileAccess::get_file_as_string(p_path);
#else

#	if JSB_USE_TYPESCRIPT
	const String path = jsb::internal::PathUtil::convert_typescript_path(p_path);
	const String source_code = FileAccess::get_file_as_string(path);
#	else
	const String path = jsb::internal::PathUtil::convert_javascript_path(p_path);
	const String source_code = FileAccess::get_file_as_string(path);
#	endif

#endif
	Error err = FileAccess::get_open_error();
	if (err != OK) {
		JSB_LOG(Warning, "can not read source from %s", p_path);
	} else {
		_set_source_code(source_code);
	}
	return err;
}

void GodotJSScript::load_module_if_missing() {
	if (!loaded_ || is_valid_internal()) return;

	JSB_LOG(Verbose, "force to load missing script %s", get_path());
	loaded_ = false;
	load_module_immediately();
}

void GodotJSScript::load_module_immediately() {
	if (loaded_) return;
	JSB_BENCHMARK_SCOPE(GodotJSScript, load_module);

	const String path = jsb::internal::PathUtil::convert_typescript_path(get_path());
	jsb::JSEnvironment env(get_path(), true);

	loaded_ = true;
	base.unref();
	source_changed_cache = true;
	jsb::JavaScriptModule *module;
	if (const Error err = env->load(path, &module); err != OK) {
		script_class_info_ = {};
#ifdef TOOLS_ENABLED
		if (FileAccess::file_exists(get_path()) && !FileAccess::file_exists(path)) {
			JSB_LOG(Error,
					"the javascript file is missing: %s (source: %s), "
					"please ensure that all typescript source files have already been compiled "
					"using the typescript compiler ('tsc').",
					path,
					get_path());
			return;
		}
#endif
		JSB_LOG(Error, "failed to attach module %s (%d)", path, err);
		return;
	}
	jsb_check(module);
	{
		const jsb::ScriptClassInfoPtr class_info_ptr = env->find_script_class(module->script_class_id);
		script_class_info_ = class_info_ptr ? (jsb::StatelessScriptClassInfo)*class_info_ptr : jsb::StatelessScriptClassInfo();
	}
	if (is_valid_internal()) {
		JSB_LOG(VeryVerbose, "GodotJSScript module loaded %s", path);
		{
			//TODO a dirty but approaching solution for hot-reloading
			//TODO will crash if reloading script instances in worker threads
			std::lock_guard lock(GodotJSScriptLanguage::get_singleton()->mutex_); // necessary?
			for (RBSet<Object *>::Element *E = instances_.front(); E;) {
				RBSet<Object *>::Element *N = E->next();
				Object *obj = E->get();
				jsb_check(obj->get_script() == Ref(this));
				jsb_check(env->verify_object(obj));

				if (ClassDB::is_parent_class(env->get_script_class(module->script_class_id)->native_class_name, obj->get_class())) {
					env->rebind(obj, module->script_class_id);
				} else {
					JSB_LOG(Warning, "Cannot rebind class %s (%s) on %s, it requires a %s", script_class_info_.js_class_name, script_class_info_.module_id, obj->get_class(), env->get_script_class(module->script_class_id)->native_class_name);
					obj->set_script(Ref<Script>());
				}

				E = N;
			}
		}

		// setup base script
		{
			//TODO do not rely on ResourceLoader
			if (!script_class_info_.base_script_module_id.is_empty()) {
				jsb::JavaScriptModule *base_module = nullptr;
				const Error err = env->load(script_class_info_.base_script_module_id, &base_module);
				jsb_ensuref(base_module && err == OK, "JS Module not found: %s", script_class_info_.base_script_module_id);
				const Ref<Resource> base_res = ResourceLoader::get_singleton()->load(jsb::internal::PathUtil::convert_javascript_path(base_module->source_info.source_filepath));
				jsb_check(base_res->get_class() == jsb_typename(GodotJSScript));
				base = base_res;
			}
		}

		// update the default value cache
		_update_exports();
#ifdef TOOLS_ENABLED
		// temp and tricky workaround to avoid missing doc when showing on inspector the first time after load
		// GDExtension 没有暴露 DocData/EditorHelp are not available in godot-cpp GDExtension
		// TODO: 可以考虑调用 EditorFileSystem::update_file() 进行触发，是有必要吗？
		// if (DocTools* doc_tools = EditorHelp::get_doc_data())
		// {
		//     const Vector<DocData::ClassDoc> documentations = get_documentation();
		//     for (int i = 0; i < documentations.size(); i++)
		//     {
		//         const DocData::ClassDoc& doc = documentations.get(i);
		//         doc_tools->add_doc(doc);
		//     }
		// }
#endif
		return;
	}
	JSB_LOG(Debug, "a stub script loaded which does not contain a GodotJS class %s", path);
}

GDExtensionScriptInstancePtr GodotJSScript::_instance_create(Object *p_for_object) const {
	return const_cast<GodotJSScript *>(this)->instance_construct_default(p_for_object)->get_extension_instance_ptr();
}

GDExtensionScriptInstancePtr GodotJSScript::_placeholder_instance_create(Object *p_this) const {
#ifdef TOOLS_ENABLED
	if (!_is_valid()) {
		JSB_LOG(Warning, "creating placeholder instance on invalid script (%s)", get_path());
	}
	PlaceholderScriptInstance *placeholder = memnew(PlaceholderScriptInstance(Ref(const_cast<GodotJSScript *>(this)), p_this));
	const_cast<GodotJSScript *>(this)->placeholders.push_back(placeholder);
	const_cast<GodotJSScript *>(this)->_update_exports_internal(placeholder);
	return placeholder->get_extension_instance_ptr();
#else
	return nullptr;
#endif
}

#ifdef TOOLS_ENABLED
void GodotJSScript::_placeholder_erased(GDExtensionScriptInstancePtr p_placeholder) {
	// 配合 ScriptLanguage 的 reload_scripts 进行逆向查找
	for (auto i = placeholders.size() - 1; i >= 0; --i) {
		PlaceholderScriptInstance *placeholder = placeholders[i];
		if (placeholder->get_extension_instance_ptr() == p_placeholder) {
			placeholders[i] = placeholders[placeholders.size() - 1];
			placeholders.resize(placeholders.size() - 1);
			memdelete(placeholder);
			return;
		}
	}

	ERR_FAIL_MSG("GodotJSScript::_placeholder_erased: There may have some memory leak issue.");
}
#endif

void GodotJSScript::_update_exports() {
	ensure_module_loaded();
	jsb_check(loaded_);
#ifdef TOOLS_ENABLED
	if (!_is_valid()) return;
	_update_exports_internal(nullptr);
#endif
}

Variant GodotJSScript::_new(const Variant **p_args, GDExtensionInt p_argcount, GDExtensionCallError &r_error) {
	if (!_is_valid()) {
		r_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
		JSB_LOG(Error, "Unable to create new instance. The script was not properly loaded (%s)", get_path());
		return Variant();
	}

	r_error.error = GDEXTENSION_CALL_OK;

	const Variant owner_var = ClassDB::instantiate(script_class_info_.native_class_name);
	Object *owner = owner_var;

	ScriptInstance *instance = instance_construct(owner, false, p_args, p_argcount);

	if (RefCounted *rc = Object::cast_to<RefCounted>(owner)) {
		if (!instance) return {};
		else return owner_var;
	} else {
		if (!instance) {
			memdelete(owner);
			return {};
		} else return owner;
	}
}

#ifdef TOOLS_ENABLED
bool GodotJSScript::_update_exports_internal(PlaceholderScriptInstance *p_placeholder_instance_to_update) {
	// do not crash the engine if the script not loaded successfully
	if (!_is_valid()) {
		JSB_LOG(Error, "script failed to load (%s)", get_path());
		return false;
	}

	bool changed = false;

	if (source_changed_cache) {
		source_changed_cache = false;
		changed = true;

		members_cache.clear();
		member_default_values_cache.clear();

		jsb::JSEnvironment env(get_path(), true);
		env->check_internal_state();

		jsb::JavaScriptModule *module = nullptr;
		const Error err = env->load(script_class_info_.module_id, &module);
		jsb_ensuref(module && err == OK, "JS Module not found: %s", script_class_info_.module_id);

		if (const jsb::ScriptClassInfoPtr class_info = env->find_script_class(module->script_class_id)) {
			for (const KeyValue<StringName, jsb::ScriptPropertyInfo> &pair : script_class_info_.properties) {
				const jsb::ScriptPropertyInfo &pi = pair.value;
				members_cache.push_back((PropertyInfo)pi);

				//TODO maybe this behaviour is not expected
				Variant default_value;
				env->get_default_property_value(*class_info, pi.name, default_value);
				member_default_values_cache[pi.name] = default_value;
				JSB_LOG(VeryVerbose, "GodotJS script default %s.%s = %s", is_valid_internal() ? script_class_info_.js_class_name : "(unknown)", pi.name, default_value);
			}
		} else {
			JSB_LOG(Warning, "ScriptClassInfo is invalid, fallback to empty default values (script %s)", get_path());
			for (const KeyValue<StringName, jsb::ScriptPropertyInfo> &pair : script_class_info_.properties) {
				const jsb::ScriptPropertyInfo &pi = pair.value;
				members_cache.push_back(pi); // 隐式 ScriptPropertyInfo 转换为 PropertyInfo

				Variant default_value;
				jsb::internal::VariantUtil::construct_variant(default_value, pi.type);
				member_default_values_cache[pi.name] = default_value;
			}
		}
	}

	if (base.is_valid() && base->_update_exports_internal(p_placeholder_instance_to_update)) {
		changed = true;
	}

	if ((changed || p_placeholder_instance_to_update) && placeholders.size()) {
		TypedArray<Dictionary> props;
		Dictionary values;
		_update_exports_values(props, values);

		if (changed) {
			for (PlaceholderScriptInstance *placeholder : placeholders) {
				placeholder->update(props, values);
			}
		} else {
			p_placeholder_instance_to_update->update(props, values);
		}
	}

	return changed;
}

void GodotJSScript::_update_exports_values(TypedArray<Dictionary> &r_props, Dictionary &r_values) {
	for (const KeyValue<StringName, Variant> &E : member_default_values_cache) {
		r_values[E.key] = E.value;
	}

#	ifdef TOOLS_ENABLED
	r_props.push_back(get_class_category().operator Dictionary());
#	endif
	for (const PropertyInfo &E : members_cache) {
		r_props.push_back(E.operator Dictionary());
	}

	if (base.is_valid() && base->_is_valid()) {
		base->_update_exports_values(r_props, r_values);
	}
}
#endif // TOOLS_ENABLED

void GodotJSScript::_bind_methods() {
	ClassDB::bind_vararg_method(METHOD_FLAGS_DEFAULT, "new", &GodotJSScript::_new, MethodInfo("new"));
}

// ============
Dictionary convert_property_info(const jsb::ScriptPropertyInfo &p_info) { return p_info.operator Dictionary(); }

TypedArray<Dictionary> GodotJSScript::_get_script_property_list() const {
	TypedArray<Dictionary> result;
	get_script_property_list<Dictionary, &convert_property_info, TypedArray<Dictionary>>(result);
	return result;
}

Dictionary convert_method_info(const StringName &p_name, const jsb::ScriptMethodInfo &p_info) {
	Dictionary dict;
	dict["name"] = p_name;
	// TODO: 其他细节
	return dict;
}

TypedArray<Dictionary> GodotJSScript::_get_script_method_list() const {
	TypedArray<Dictionary> result;
	get_script_method_list<Dictionary, &convert_method_info, TypedArray<Dictionary>>(result);
	return result;
}
Dictionary convert_signal_info(const StringName &p_name, const jsb::ScriptSignalInfo &p_info) {
	Dictionary dict;
	dict["name"] = p_name;
	// TODO: 其他细节
	return dict;
}
TypedArray<Dictionary> GodotJSScript::_get_script_signal_list() const {
	TypedArray<Dictionary> result;
	get_script_signal_list<Dictionary, &convert_signal_info, TypedArray<Dictionary>>(result);
	return result;
}
