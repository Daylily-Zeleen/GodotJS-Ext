#include "jsb_object_bindings.h"
#include "jsb_transpiler.h"
#include "jsb_type_convert.h"
#include "api_tool/api_tool.h"
// TODO: Refactor. Violates isolation of bridge.
#include "../weaver/jsb_script_instance.h"
#include "../weaver/jsb_script_language.h"

#include <godot_cpp/classes/engine.hpp>

namespace jsb
{
    static Variant::Type sanitize_return_type(const Variant::Type p_metadata_type, const Variant& p_return_value)
    {
        const int type_index = static_cast<int>(p_metadata_type);
        if (type_index >= static_cast<int>(Variant::NIL) && type_index < static_cast<int>(Variant::VARIANT_MAX))
        {
            return p_metadata_type;
        }
        return p_return_value.get_type();
    }

    /** TODO: 
            仅对 RefCounted, 额外添加 [Symbol.dispose] 与 [Symbol.asyncDispose] 以支持离开 using 作用域时释放资源资源， env->dispose_binding_object
            注意文档也要相应生成
     */
    NativeClassInfoPtr ObjectReflectBindingUtil::reflect_bind(Environment* p_env, const godot::StringName& p_class_name, NativeClassID* r_class_id)
    {
        v8::Isolate* isolate = p_env->get_isolate();
        v8::HandleScope handle_scope(isolate);

        jsb_check(!p_class_name.is_empty());

        String class_name = internal::NamingUtil::get_class_name(p_class_name);
        const NativeClassID class_id = p_env->add_native_class(NativeClassType::GodotObject, class_name);
        JSB_LOG(VeryVerbose, "expose godot type %s(%d) as %s", p_class_name, class_id, class_name);

        // construct type template
        {
            impl::ClassBuilder class_builder = ObjectTemplate::create(p_env, class_id);

            //NOTE all singleton object will overwrite the class itself in 'godot' module, so we need make all things defined on PrototypeTemplate.
            const bool is_singleton_class = Engine::get_singleton()->has_singleton(p_class_name);
            auto static_builder = is_singleton_class ? class_builder.Instance() : class_builder.Static();

            const api_tool::ApiClass* api_class = api_tool::find_class(p_class_name);
            jsb_check(api_class);
#if JSB_EXCLUDE_GETSET_METHODS
            HashSet<StringName> omitted_methods;
#endif
            // class: properties (getset)
            for (const api_tool::ApiPropertyInfo& prop : api_class->properties)
            {
                StringName prop_name = prop.property.name;
                if (internal::StringNames::get_singleton().is_ignored(prop_name)) continue;

                const StringName& property_name = internal::NamingUtil::get_member_name(prop_name);
                const int prop_index = prop.index;
                const StringName getter_name = prop.getter;
                const StringName setter_name = prop.setter;

                const api_tool::ApiClassMethod* getter_method = nullptr;
                const api_tool::ApiClassMethod* setter_method = nullptr;

                for (const auto& method_info : api_class->methods)
                {
                    if (method_info.method.name == getter_name)
                    {
                        getter_method = &method_info;
                    }
                    if (method_info.method.name == setter_name)
                    {
                        setter_method = &method_info;
                    }
                    if(getter_method && setter_method) break;
                }

                if (prop_index >= 0)
                {
                    const int remap_index = (int) p_env->get_variant_info_collection().object_properties.size();
                    internal::FPropertyInfo2 property_info2;
                    property_info2.getter_func = getter_method;
                    property_info2.setter_func = setter_method;
                    property_info2.index = prop_index;
                    p_env->get_variant_info_collection().object_properties.append(property_info2);

                    class_builder.Instance().Property(property_name,
                        getter_method ? _godot_object_get2 : nullptr,
                        setter_method ? _godot_object_set2 : nullptr, remap_index);
                }
                else
                {
                    // TODO: 改用更简单的访问器回调取代 _godot_object_method
                    class_builder.Instance().Property(property_name,
                        getter_method ? _godot_object_method : nullptr, (void*) getter_method,
                        setter_method ? _godot_object_method : nullptr, (void*) setter_method);

#if JSB_EXCLUDE_GETSET_METHODS
                    if (internal::VariantUtil::is_valid_name(getter_name)) omitted_methods.insert(getter_name);
                    if (internal::VariantUtil::is_valid_name(setter_name)) omitted_methods.insert(setter_name);
#endif
                }
            }

            // class: methods
            for (const api_tool::ApiClassMethod& method_info : api_class->methods)
            {
                if (method_info.is_virtual()) continue; // 虚函数不需要绑定
#if JSB_EXCLUDE_GETSET_METHODS
                if (omitted_methods.has(method_info.method.name)) continue;
#endif
                const StringName& method_name = internal::NamingUtil::get_member_name(method_info.method.name);

                if (method_info.method.flags & METHOD_FLAG_STATIC)
                {
                    static_builder.Method(method_name, _godot_object_method, (void*) &method_info);
                }
                else
                {
                    class_builder.Instance().Method(method_name, _godot_object_method, (void*) &method_info);
                }
            }

            if (p_class_name == jsb_string_name(Object))
            {
                // class: special methods
                class_builder.Instance().Method(jsb_literal(free), _godot_object_free);
            }

             // class: signals
            for (const api_tool::ApiSignalInfo& signal_info : api_class->signals)
            {
                v8::HandleScope handle_scope_for_enum(isolate);
                StringName signal_name_sn = signal_info.name;
                String signal_name = internal::NamingUtil::get_member_name(signal_name_sn);
                const v8::Local<v8::String> signal_name_js = p_env->get_string_name_cache().get_string_value(isolate, signal_name_sn);
                class_builder.Instance().Property(signal_name, _godot_object_signal_get, signal_name_js.As<v8::Value>());
            }

            HashSet<StringName> enum_consts;

             // class: enum (nested in class)
            for (const api_tool::ApiEnumInfo& enum_info : api_class->enums)
            {
                StringName enum_name = enum_info.name;
                impl::ClassBuilder::EnumDeclaration enumeration = static_builder.Enum(internal::NamingUtil::get_enum_name(enum_name));
                for (const api_tool::ApiEnumValue& enum_value : enum_info.values)
                {
                    StringName constant_name = enum_value.name;
                    const String& js_enum_name = internal::NamingUtil::get_enum_value_name(constant_name);
                    jsb_not_implemented(js_enum_name.contains("."), "hierarchically nested definition is currently not supported");
                    enumeration.Value(js_enum_name, enum_value.value);
                    enum_consts.insert(constant_name);
                }
            }

            // class: constants
            for (const api_tool::ApiConstantInfo& constant_info : api_class->constants)
            {
                StringName constant_name = constant_info.name;
                if (enum_consts.has(constant_name)) continue;
                const String& js_const_name = (String) internal::NamingUtil::get_constant_name(constant_name);
                jsb_not_implemented(js_const_name.contains("."), "hierarchically nested definition is currently not supported");

                static_builder.Value(constant_name, constant_info.value);
            }

            // set `class_id` on the exposed godot native class for the convenience when finding it from any subclasses in javascript.
            class_builder.Static().Value(jsb_symbol(p_env, ClassId), *class_id);

            // build the prototype chain (inherit)
            if (!api_class->inherits.is_empty())
            {
                if (NativeClassID super_class_id;
                    const NativeClassInfoPtr super_class_info = p_env->expose_godot_object_class(api_class->inherits, &super_class_id))
                {
                    class_builder.Inherit(super_class_info->clazz);
                    JSB_LOG(VeryVerbose, "%s (%d) extends %s (%d)", p_class_name, class_id, api_class->inherits, super_class_id);
                }
            }

            // preparation for return
            {
                NativeClassInfoPtr class_info = p_env->get_native_class(class_id);

                class_info->clazz = class_builder.Build();
                jsb_check(!class_info->clazz.IsEmpty());
                jsb_check(class_info->name == internal::NamingUtil::get_class_name(p_class_name));
                JSB_LOG(VeryVerbose, "build class info %s (%d) exposed as %s, addr: %s", p_class_name, class_id, class_info->name, class_info.ptr());
                if (r_class_id) *r_class_id = class_id;
                return class_info;
            }
        } // end type template block scope
    }

    void ObjectReflectBindingUtil::_godot_object_signal_get(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        v8::Isolate* isolate = info.GetIsolate();
        const v8::Local<v8::Context> context = isolate->GetCurrentContext();

        Environment* environment = Environment::wrap(isolate);
        jsb_check(info.Data()->IsString());
        const StringName gd_signal_name = environment->get_string_name_cache().get_string_name(isolate, info.Data().As<v8::String>());

        const v8::Local<v8::Object> self = info.This();
        // (assume) debugger may trigger the property getter of signal without an instance
        if (self.IsEmpty() || !self->IsObject())
        {
            return;
        }

        // strict check for Godot Object
        void* pointer = environment->get_verified_object(self, NativeClassType::GodotObject);
        if (!pointer)
        {
            const String error_message = jsb_errorf("failure obtaining signal: %s. signal owner is undefined or dead.", gd_signal_name);
            jsb_throw(isolate, error_message);
            return;
        }

        // signal must be instance-owned
        Object* gd_object = (Object*) pointer;
        if (v8::Local<v8::Value> rval; TypeConvert::gd_var_to_js(isolate, context, Signal(gd_object, gd_signal_name), rval))
        {
            info.GetReturnValue().Set(rval);
            return;
        }
        const String error_message = jsb_errorf("failure obtaining signal: %s. bad signal", gd_signal_name);
        jsb_throw(isolate, error_message);
    }

    void ObjectReflectBindingUtil::_godot_object_cached_export_update(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        v8::Isolate* isolate = info.GetIsolate();
        const v8::Local<v8::Context> context = isolate->GetCurrentContext();

        Environment* environment = Environment::wrap(isolate);
        jsb_check(info.Data()->IsString());

        const v8::Local<v8::Object> self = info.This();

        void* pointer = environment->get_verified_object(self, NativeClassType::GodotObject);

        if (!pointer)
        {
            // Ignore. This may occur if a JS constructor/initializer sets the property (the object is not yet bound).
            // GodotJSScriptInstance's postbind will establish the initial cache, so this is safe to ignore.
            return;
        }

        const StringName property_name = environment->get_string_name_cache().get_string_name(isolate, info.Data().As<v8::String>());

        GodotJSScriptInstance *script_instance = ScriptInstance::get_script_instance<GodotJSScriptInstance>((Object *)pointer);
        if (!script_instance)
        {
            // Again, this could conceivably occur during construction. Safe to ignore.
            return;
        }

        v8::Local<v8::Value> js_value;

        if (info.Length() > 0)
        {
            // Auto-accessor pass the latest value in, saving an unnecessary get
            js_value = info[0];
        }
        else
        {
            v8::MaybeLocal<v8::Value> get_result = self->Get(context, info.Data());

            if (get_result.IsEmpty())
            {
                const String error_message = jsb_errorf("failure setting cached export: %s. failed to get latest value", property_name);
                jsb_throw(isolate, error_message);
                return;
            }

            js_value = get_result.ToLocalChecked();
        }

        Variant gd_value;

        if (!TypeConvert::js_to_gd_var(isolate, context, js_value, gd_value))
        {
            const String error_message = jsb_errorf("failure setting cached export: %s. failed to convert value to a variant", property_name);
            jsb_throw(isolate, error_message);
            return;
        }

        script_instance->cache_property(property_name, gd_value);
    }

    void ObjectReflectBindingUtil::_godot_object_free(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        v8::Isolate* isolate = info.GetIsolate();
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        Object* gd_object;
        if (!TypeConvert::js_to_gd_obj(isolate, context, info.This(), gd_object) || !gd_object)
        {
            jsb_throw(isolate, "bad this");
            return;
        }

        Variant dummy;
        GDExtensionCallError error {};
        Variant(gd_object).callp(jsb_string_name(free), nullptr, 0, dummy, error);
        jsb_check(dummy.get_type() == Variant::NIL);
        if (jsb_unlikely(error.error != GDEXTENSION_CALL_OK))
        {
            jsb_throw(isolate, "bad free");
            return;
        }
    }

    void ObjectReflectBindingUtil::_godot_utility_func(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        v8::Isolate* isolate = info.GetIsolate();
        const v8::Local<v8::Context> context = isolate->GetCurrentContext();
        const internal::FUtilityMethodInfo& method_info = Environment::wrap(context)->get_variant_info_collection().utility_funcs[info.Data().As<v8::Int32>()->Value()];
        const int argc = info.Length();

        // prepare argv
        if (!method_info.check_argc(argc))
        {
            const String error_message = jsb_errorf("%d arguments are required", method_info.argument_types.size());
            jsb_throw(isolate, error_message);
            return;
        }
        const Variant** argv = jsb_stackalloc(const Variant*, argc);
        const int known_argc = (int) method_info.argument_types.size();
        Variant* args = jsb_stackalloc(Variant, argc);
        for (int index = 0; index < argc; ++index)
        {
            memnew_placement(&args[index], Variant);
            argv[index] = &args[index];
            if (index < known_argc
                ? !TypeConvert::js_to_gd_var(isolate, context, info[index], method_info.argument_types[index], args[index])
                : !TypeConvert::js_to_gd_var(isolate, context, info[index], args[index]))
            {
                // revert all constructors
                const String error_message = index < known_argc
                    ? jsb_errorf("Bad argument: %d. Unable to convert JS %s to Godot %s", index, TypeConvert::js_debug_typeof(isolate, info[index]), Variant::get_type_name(method_info.argument_types[index]))
                    : jsb_errorf("Bad argument: %d. Unable to convert JS %s", index, TypeConvert::js_debug_typeof(isolate, info[index]));
                while (index >= 0) { args[index--].~Variant(); }
                jsb_throw(isolate, error_message);
                return;
            }
        }

        // call godot method
        Variant crval;
        method_info.utility_func->validated_call(&crval, argv, argc);

        // don't forget to destruct all stack allocated variants
        for (int index = 0; index < argc; ++index)
        {
            args[index].~Variant();
        }

        v8::Local<v8::Value> jrval;
        if (TypeConvert::gd_var_to_js(isolate, context, crval, jrval))
        {
            info.GetReturnValue().Set(jrval);
            return;
        }
        const String error_message = jsb_errorf("Failed to translate returned Godot %s to a JS value", Variant::get_type_name(crval.get_type()));
        jsb_throw(isolate, error_message);
    }

    void ObjectReflectBindingUtil::_godot_object_method(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        jsb_check(info.Data()->IsExternal());
        v8::Isolate* isolate = info.GetIsolate();
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        const api_tool::ApiClassMethod* method_info = (api_tool::ApiClassMethod*) info.Data().As<v8::External>()->Value();
        const int argc = info.Length();

        jsb_check(method_info);
        Environment::wrap(isolate)->check_internal_state();
        Object* gd_object = nullptr;
        if (!method_info->is_static())
        {
            if (!TypeConvert::js_to_gd_obj(isolate, context, info.This(), gd_object) || !gd_object)
            {
                const String error_message = jsb_errorf("Failed to call: %s. Bad this", method_info->method.name);
                jsb_throw(isolate, error_message);
                return;
            }
        }

        // prepare argv
        const int method_argc = method_info->method.arguments.size();
        const bool method_is_vararg = method_info->is_vararg();

        if (!internal::VariantUtil::check_argc(method_is_vararg, argc, method_info->method.default_arguments.size(), method_argc))
        {
            const String error_message = jsb_errorf("Failed to call: %s. %d arguments are required", method_info->method.name, method_argc - method_info->method.default_arguments.size());
            jsb_throw(isolate, error_message);
            return;
        }
        const Variant** argv = jsb_stackalloc(const Variant*, argc);
        Variant* args = jsb_stackalloc(Variant, argc);
        for (int index = 0; index < argc; ++index)
        {
            memnew_placement(&args[index], Variant);
            argv[index] = &args[index];
            const Variant::Type type = index >= method_argc
                ? Variant::Type::NIL
                : (Variant::Type) method_info->method.arguments[index].type;

            const v8::Local<v8::Value>& argument = info[index];

            if (argument->IsUndefined() && method_info->method.default_arguments.size() > 0)
            {
                args[index] = method_info->method.default_arguments[index - method_argc];
            }
            else if (!TypeConvert::js_to_gd_var(isolate, context, argument, type, args[index]))
            {
                // revert all constructors
                const String error_message = jsb_errorf("Failed to call: %s. Bad argument: %d. Unable to convert JS %s to Godot %s", method_info->method.name, index, TypeConvert::js_debug_typeof(isolate, info[index]), Variant::get_type_name(type));
                while (index >= 0) { args[index--].~Variant(); }
                jsb_throw(isolate, error_message);
                return;
            }
        }

        // call godot method
        GDExtensionCallError error {};
        Variant crval = method_info->validated_call(gd_object, argv, argc, error);

        // don't forget to destruct all stack allocated variants
        for (int index = 0; index < argc; ++index)
        {
            args[index].~Variant();
        }

        if (error.error != GDEXTENSION_CALL_OK)
        {
            const String error_message = jsb_errorf("Failed to call: %s", method_info->method.name);
            jsb_throw(isolate, error_message);
            return;
        }
        v8::Local<v8::Value> jrval;
        const Variant::Type return_type = sanitize_return_type((Variant::Type) method_info->method.return_val.type, crval);
        jsb_check(return_type == method_info->method.return_val.type);
        if (TypeConvert::gd_var_to_js(isolate, context, crval, return_type, jrval))
        {
            info.GetReturnValue().Set(jrval);
            return;
        }
        const String error_message = jsb_errorf(
            "Failed to return from call: %s. "
            "Failed to translate returned Godot %s to a JS value",
            method_info->method.name,
            Variant::get_type_name(crval.get_type()));
        jsb_throw(isolate, error_message);
    }

    void ObjectReflectBindingUtil::_godot_object_get2(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        jsb_check(info.Data()->IsInt32());
        v8::Isolate* isolate = info.GetIsolate();
        Environment* env = Environment::wrap(isolate);
        const v8::Local<v8::Context> context = isolate->GetCurrentContext();
        const internal::FPropertyInfo2& property_info = env->get_variant_info_collection().object_properties[info.Data().As<v8::Int32>()->Value()];
        env->check_internal_state();
        // prepare argv
        if (info.Length() != 0)
        {
            const String error_message = jsb_errorf("Failed to get property: %s. Arguments unexpectedly provided", property_info.getter_func->method.name);
            jsb_throw(isolate, error_message);
            return;
        }

        Object* gd_object = nullptr;
        if (!property_info.getter_func->is_static() && (!TypeConvert::js_to_gd_obj(isolate, context, info.This(), gd_object) || !gd_object))
        {
            const String error_message = jsb_errorf("Failed to get property: %s. Bad this", property_info.getter_func->method.name);
            jsb_throw(isolate, error_message);
            return;
        }

        Variant args[] = { property_info.index };
        const Variant* argv[] = { &args[0] };

        // call godot method
        GDExtensionCallError error {};
        Variant crval = property_info.getter_func->validated_call(gd_object, argv, 1, error);

        if (error.error != GDEXTENSION_CALL_OK)
        {
            const String error_message = jsb_errorf("Failed to get property: %s. Execution failed", property_info.getter_func->method.name);
            jsb_throw(isolate, error_message);
            return;
        }
        v8::Local<v8::Value> jrval;
        const Variant::Type return_type = sanitize_return_type((Variant::Type) property_info.getter_func->method.return_val.type, crval);
        jsb_check(return_type == property_info.getter_func->method.return_val.type);
        if (TypeConvert::gd_var_to_js(isolate, context, crval, return_type, jrval))
        {
            info.GetReturnValue().Set(jrval);
            return;
        }
        const String error_message = jsb_errorf("Failed to get property: %s. Failed to translate returned Godot %s to a JS value",
            property_info.getter_func->method.name, Variant::get_type_name(crval.get_type()));
        jsb_throw(isolate, error_message);
    }

    void ObjectReflectBindingUtil::_godot_object_set2(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        jsb_check(info.Data()->IsInt32());
        v8::Isolate* isolate = info.GetIsolate();
        Environment* env = Environment::wrap(isolate);
        const v8::Local<v8::Context> context = isolate->GetCurrentContext();
        const internal::FPropertyInfo2& property_info = env->get_variant_info_collection().object_properties[info.Data().As<v8::Int32>()->Value()];
        env->check_internal_state();
        // prepare argv
        if (info.Length() != 1)
        {
            const String error_message = jsb_errorf("Failed to set property: %s. 1 argument is required", property_info.setter_func->method.name);
            jsb_throw(isolate, error_message);
            return;
        }

        Object* gd_object = nullptr;
        if (!property_info.setter_func->is_static() && (!TypeConvert::js_to_gd_obj(isolate, context, info.This(), gd_object) || !gd_object))
        {
            const String error_message = jsb_errorf("Failed to set property: %s. Bad this", property_info.setter_func->method.name);
            jsb_throw(isolate, error_message);
            return;
        }

        Variant cvar;
        if (!TypeConvert::js_to_gd_var(isolate, context, info[0], (Variant::Type) property_info.setter_func->method.arguments[0].type, cvar))
        {
            const String error_message = jsb_errorf("Failed to set property: %s. Unable to convert provided JS %s to Godot %s",
                property_info.setter_func->method.name, TypeConvert::js_debug_typeof(isolate, info[0]),
                Variant::get_type_name((Variant::Type) property_info.setter_func->method.arguments[0].type));
            jsb_throw(isolate, error_message);
            return;
        }

        Variant args[] = { property_info.index, cvar };
        const Variant* argv[] = { &args[0], &args[1] };

        // call godot method
        GDExtensionCallError error {};
        property_info.setter_func->validated_call(gd_object, argv, ::std::size(argv), error);

        if (error.error != GDEXTENSION_CALL_OK)
        {
            const String error_message = jsb_errorf("Failed to set property: %s. Execution failed", property_info.setter_func->method.name);
            jsb_throw(isolate, error_message);
            return;
        }
    }

}
