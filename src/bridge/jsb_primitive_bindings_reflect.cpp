#include "jsb_primitive_bindings_reflect.h"
#if !JSB_WITH_STATIC_BINDINGS
#include "jsb_reflect_binding_util.h"
#include "jsb_class_register.h"
#include "jsb_class_info.h"
#include "jsb_transpiler.h"
#include "jsb_bridge_helper.h"
#include "jsb_type_convert.h"
#include "../internal/jsb_variant_info.h"
#include "../internal/jsb_variant_util.h"
#include "api_tool/api_tool_types.h"

#define JSB_DEFINE_OPERATOR2(op_code) class_builder.Static().\
    Method(JSB_OPERATOR_NAME(op_code), BinaryOperator::invoke, (int32_t) Variant::OP_##op_code);\
    JSB_LOG(VeryVerbose, "generate %d: %s", Variant::OP_##op_code, JSB_OPERATOR_NAME(op_code));

#define JSB_DEFINE_OPERATOR1(op_code) class_builder.Static().\
    Method(JSB_OPERATOR_NAME(op_code), UnaryOperator::invoke, (int32_t) Variant::OP_##op_code);\
    JSB_LOG(VeryVerbose, "generate %d: %s", Variant::OP_##op_code, JSB_OPERATOR_NAME(op_code));

#if JSB_FAST_REFLECTION
#define JSB_DEFINE_FAST_GETSET(ForMemberVariantType, ForMemberCppType, PropName, MemberPtr) \
    if (TReflectGetSetPointerCall<T, ForMemberCppType>::is_supported(ForMemberVariantType))\
    {\
        class_builder.Instance().Property(PropName,\
            TReflectGetSetPointerCall<T, ForMemberCppType>::_getter, (void * )(MemberPtr)->get_getter_ptr(),\
            TReflectGetSetPointerCall<T, ForMemberCppType>::_setter, (void * )(MemberPtr)->get_setter_ptr()\
        );\
        continue;\
    } (void) 0
#define JSB_DEFINE_FAST_CONSTRUCTOR(ForCppType, ClassID, ClassName) \
    if constexpr (ReflectConstructorCall<ForCppType>::is_supported(TYPE))\
    {\
        return impl::ClassBuilder::New<IF_VariantFieldCount>(p_env.isolate, (ClassName), &ReflectConstructorCall<ForCppType>::constructor, *(ClassID));\
    } (void) 0
#else
#define JSB_DEFINE_FAST_GETSET(ForMemberVariantType, ForMemberCppType, PropName, MemberPtr) (void) 0
#define JSB_DEFINE_FAST_CONSTRUCTOR(ForCppType, ClassID, ClassName) (void) 0
#endif

#define JSB_DEFINE_OVERLOADED_BINARY_BEGIN(op_code) JSB_DEFINE_OPERATOR2(op_code)
#define JSB_DEFINE_OVERLOADED_BINARY_END()

#define JSB_DEFINE_BINARY_OVERLOAD(R, A, B)
#define JSB_DEFINE_UNARY(op_code) JSB_DEFINE_OPERATOR1(op_code)
#define JSB_DEFINE_COMPARATOR(op_code) JSB_DEFINE_OPERATOR2(op_code)

#define JSB_TYPE_BEGIN(InType) \
    template<>\
    struct OperatorRegister<InType>\
    {\
        typedef InType CurrentType;\
        static void generate(impl::ClassBuilder& class_builder)\
        {\
            JSB_LOG(VeryVerbose, "expose primitive type " #InType);

#define JSB_TYPE_END() \
        }\
    };

namespace jsb
{
    struct BinaryOperator
    {
        static void invoke(const v8::FunctionCallbackInfo<v8::Value>& info)
        {
            v8::Isolate* isolate = info.GetIsolate();
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            const Variant::Operator op = (Variant::Operator) info.Data().As<v8::Int32>()->Value();
            if (info.Length() != 2)
            {
                jsb_throw(isolate, "bad param");
                return;
            }
            Variant left, right;
            if (!TypeConvert::js_to_gd_var(isolate, context, info[0], left) || !TypeConvert::js_to_gd_var(isolate, context, info[1], right))
            {
                jsb_throw(isolate, "bad translation");
                return;
            }
            Variant ret;
            bool r_valid = false;
            Variant::evaluate(op, left, right, ret, r_valid);
            if (!r_valid)
            {
                jsb_throw(isolate, jsb_format(
                    "bad operation(%s) between %s and %s.", 
                    api_tool::get_variant_operator_name(op),
                    Variant::get_type_name(left.get_type()),
                    Variant::get_type_name(right.get_type())));
                return;
            }

            v8::Local<v8::Value> rval;
            if (!TypeConvert::gd_var_to_js(isolate, context, ret, rval))
            {
                jsb_throw(isolate, "bad translation");
                return;
            }
            info.GetReturnValue().Set(rval);
        }
    };

    struct UnaryOperator
    {
        static void invoke(const v8::FunctionCallbackInfo<v8::Value>& info)
        {
            v8::Isolate* isolate = info.GetIsolate();
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            const Variant::Operator op = (Variant::Operator) info.Data().As<v8::Int32>()->Value();
            if (info.Length() != 1)
            {
                jsb_throw(isolate, "bad param");
                return;
            }
            Variant left;
            const Variant right; // it's not really used
            if (!TypeConvert::js_to_gd_var(isolate, context, info[0], left))
            {
                jsb_throw(isolate, "bad translation");
                return;
            }
            Variant ret;
            bool r_valid = false;
            Variant::evaluate(op, left, right, ret, r_valid);
            if (!r_valid)
            {
                jsb_throw(isolate, jsb_format(
                    "bad operation(%s) on %s.",
                    api_tool::get_variant_operator_name(op),
                    Variant::get_type_name(left.get_type())));
                return;
            }

            v8::Local<v8::Value> rval;
            if (!TypeConvert::gd_var_to_js(isolate, context, ret, rval))
            {
                jsb_throw(isolate, "bad translation");
                return;
            }
            info.GetReturnValue().Set(rval);
        }
    };

    template<typename TypeName>
    struct OperatorRegister
    {
        static void generate(impl::ClassBuilder& class_builder) {}
    };

    #define Number double
    #include "../internal/jsb_primitive_operators.def.h"
    #undef Number

    struct VariantBindFallbacks
    {
        static void constructor(const v8::FunctionCallbackInfo<v8::Value>& info)
        {
            v8::Isolate* isolate = info.GetIsolate();
            v8::Local<v8::Context> context = isolate->GetCurrentContext();

            if (!info.IsConstructCall())
            {
                jsb_throw(isolate, "bad constructor call");
                return;
            }
            const v8::Local<v8::Object> self = info.This();
            jsb_quickjs_check(self->IsObject());
            Environment* env = Environment::wrap(isolate);
            const internal::FConstructorInfo& constructor_info = GetVariantInfoCollection(env).constructors[info.Data().As<v8::Uint32>()->Value()];

            const int argc = info.Length();

            const int constructor_count = (int) constructor_info.variants.size();
            for (int constructor_index = 0; constructor_index < constructor_count; ++constructor_index)
            {
                const internal::FConstructorVariantInfo& constructor_variant = constructor_info.variants[constructor_index];
                if (constructor_variant.argument_types.size() != argc) continue;
                bool argument_type_match = true;
                for (int argument_index = 0; argument_index < argc; ++argument_index)
                {
                    const Variant::Type argument_type = constructor_variant.argument_types[argument_index];
                    if (!TypeConvert::can_convert_strict(isolate, context, info[argument_index], argument_type))
                    {
                        argument_type_match = false;
                        break;
                    }
                }

                if (!argument_type_match)
                {
                    continue;
                }

                const Variant** argv = jsb_stackalloc(const Variant*, argc);
                Variant* args = jsb_stackalloc(Variant, argc);
                for (int argument_index = 0; argument_index < argc; ++argument_index)
                {
                    memnew_placement(&args[argument_index], Variant);
                    argv[argument_index] = &args[argument_index];
                    const Variant::Type argument_type = constructor_variant.argument_types[argument_index];
                    if (!TypeConvert::js_to_gd_var(isolate, context, info[argument_index], argument_type, args[argument_index]))
                    {
                        // revert all constructors
                        const String error_message = jsb_errorf("bad argument: %d", argument_index);
                        while (argument_index >= 0) { args[argument_index--].~Variant(); }
                        jsb_throw(isolate, error_message);
                        return;
                    }

                    jsb_checkf(Variant::can_convert_strict(args[argument_index].get_type(), argument_type),
                        "Realm::can_convert_strict returned inconsistent type %s while %s is expected",
                        Variant::get_type_name(args[argument_index].get_type()),
                        Variant::get_type_name(argument_type));
                }

                // we only need to alloc a dummy instance here because the validated constructor will cast it to the expected type by itself
                // BE CAUTIOUS: DON'T FORGET TO call `Environment::dealloc_variant(instance)` if `bind_valuetype` is not eventually called
                Variant* instance = env->alloc_variant();
                *instance = constructor_variant.constructor_info->validated_construct(argv, argc);

                // don't forget to destruct all stack allocated variants
                for (int index = 0; index < argc; ++index)
                {
                    args[index].~Variant();
                }

                env->bind_valuetype(instance, self);
                return;
            }

            jsb_throw(isolate, "no suitable constructor");
        }
    };

    template<typename T>
    struct VariantConstructor
    {
        jsb_force_inline static void constructor(const v8::FunctionCallbackInfo<v8::Value>& info)
        {
            VariantBindFallbacks::constructor(info);
        }
    };

    template<typename T>
    struct VariantBind
    {
    private:
        _FORCE_INLINE_ static const api_tool::ApiBuiltinClass *get_api_info()
        {
            return api_tool::find_builtin_class(Variant::get_type_name(TYPE));
        }
    public:
        static constexpr Variant::Type TYPE = static_cast<Variant::Type>(GetTypeInfo<T>::VARIANT_TYPE);

        static void _get_constant_value_lazy(v8::Local<v8::Name> name, const v8::PropertyCallbackInfo<v8::Value>& info)
        {
            v8::Isolate* isolate = info.GetIsolate();
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            const StringName constant = impl::Helper::to_string(isolate, name);
            bool valid {false};

            Variant constant_value;
            for (const auto &api_constant: get_api_info()->constants)
            {
                if (api_constant.name == constant)
                {
                    constant_value = api_constant.value;
                    valid = true;
                    break;
                }
            }
            jsb_check(valid);

            v8::Local<v8::Value> rval;
            if (!TypeConvert::gd_var_to_js(isolate, context, constant_value, rval))
            {
                jsb_throw(isolate, "bad translate");
                return;
            }
            info.GetReturnValue().Set(rval);
        }

        //NOTE should never be called any more, since all valuetype bindings exist without a normal gc callback (object_gc_callback)
        static void finalizer(Environment* environment, void* pointer, FinalizationType p_finalize)
        {
            jsb_check(false);
            Variant* self = (Variant*) pointer;
            jsb_checkf(Variant::can_convert(self->get_type(), TYPE), "variant type can't convert to %s from %s", Variant::get_type_name(TYPE), Variant::get_type_name(self->get_type()));
            jsb_check(p_finalize != FinalizationType::None);
            environment->dealloc_variant(self);
        }

        static void _getter(const v8::FunctionCallbackInfo<v8::Value>& info)
        {
            v8::Isolate* isolate = info.GetIsolate();
            const v8::Local<v8::Context> context = isolate->GetCurrentContext();
            jsb_check(TypeConvert::is_variant(info.This()));
            const Variant* p_self = (Variant*)info.This()->GetAlignedPointerFromInternalField(IF_Pointer);
            const internal::FPrimitiveMemberInfo& member = GetVariantInfoCollection(Environment::wrap(context)).primitive_members[info.Data().As<v8::Int32>()->Value()];

            Variant value;
            internal::VariantUtil::construct_variant(value, member.member_info->type);

            //NOTE the getter function will not touch the type of `Variant`, so we must set it properly before use in the above code
            member.member_info->getter_validated_call(*p_self, value);
            v8::Local<v8::Value> rval;
            if (!TypeConvert::gd_var_to_js(isolate, context, value, rval))
            {
                jsb_throw(isolate, "bad translate");
                return;
            }
            info.GetReturnValue().Set(rval);
        }

        static void _setter(const v8::FunctionCallbackInfo<v8::Value>& info)
        {
            v8::Isolate* isolate = info.GetIsolate();
            const v8::Local<v8::Context> context = isolate->GetCurrentContext();
            jsb_check(TypeConvert::is_variant(info.This()));
            Variant* p_self = (Variant*) info.This()->GetAlignedPointerFromInternalField(IF_Pointer);
            const internal::FPrimitiveMemberInfo& member = GetVariantInfoCollection(Environment::wrap(context)).primitive_members[info.Data().As<v8::Int32>()->Value()];

            Variant value;
            if (!TypeConvert::js_to_gd_var(isolate, context, info[0], member.member_info->type, value))
            {
                jsb_throw(isolate, "bad translate");
                return;
            }
            member.member_info->setter_validated_call(*p_self, value);
        }

        static void _set_indexed(const v8::FunctionCallbackInfo<v8::Value>& info)
        {
            v8::Isolate* isolate = info.GetIsolate();
            const v8::Local<v8::Context> context = isolate->GetCurrentContext();
            jsb_check(TypeConvert::is_variant(info.This()));
            const Variant::Type element_type = get_api_info()->indexing_type;
            if (info.Length() != 2
                || !info[0]->IsNumber() // loose int32 check
                || !TypeConvert::can_convert_strict(isolate, context, info[1], element_type))
            {
                jsb_throw(isolate, "bad params");
                return;
            }
            const int32_t index = info[0].As<v8::Int32>()->Value();
            Variant value;
            if (!TypeConvert::js_to_gd_var(isolate, context, info[1], element_type, value))
            {
                jsb_throw(isolate, "bad value");
                return;
            }
            bool r_valid, r_oob;
            Variant* self = (Variant*) info.This()->GetAlignedPointerFromInternalField(IF_Pointer);
            self->set_indexed(index, value, r_valid, r_oob);
            if (!r_valid || r_oob)
            {
                jsb_throw(isolate, "invalid or out of bound");
                return;
            }
        }

        static void _get_indexed(const v8::FunctionCallbackInfo<v8::Value>& info)
        {
            v8::Isolate* isolate = info.GetIsolate();
            const v8::Local<v8::Context> context = isolate->GetCurrentContext();
            jsb_check(TypeConvert::is_variant(info.This()));
            if (info.Length() != 1 || !info[0]->IsNumber()) // loose int32 check
            {
                jsb_throw(isolate, "bad params");
                return;
            }
            const int32_t index = info[0].As<v8::Int32>()->Value();
            bool r_valid, r_oob;
            const Variant* self = (Variant*) info.This()->GetAlignedPointerFromInternalField(IF_Pointer);
            const Variant value = self->get_indexed(index, r_valid, r_oob);
            if (!r_valid || r_oob)
            {
                jsb_throw(isolate, "invalid or out of bound");
                return;
            }
            v8::Local<v8::Value> r_val;
            // nil type is treated as any type
            if (const Variant::Type element_type = get_api_info()->indexing_type;
                !TypeConvert::gd_var_to_js(isolate, context, value, element_type, r_val))
            {
                jsb_throw(isolate, "bad translation");
                return;
            }
            info.GetReturnValue().Set(r_val);
        }

        static void _set_keyed(const v8::FunctionCallbackInfo<v8::Value>& info)
        {
            v8::Isolate* isolate = info.GetIsolate();
            const v8::Local<v8::Context> context = isolate->GetCurrentContext();
            jsb_check(TypeConvert::is_variant(info.This()));
            if (info.Length() != 2)
            {
                jsb_throw(isolate, "bad params");
                return;
            }

            //TODO it's restricted since we don't know anything about the type
            Variant key;
            Variant value;
            if (!TypeConvert::js_to_gd_var(isolate, context, info[0], key)
                || !TypeConvert::js_to_gd_var(isolate, context, info[1], value))
            {
                jsb_throw(isolate, "bad value");
                return;
            }
            bool r_valid;
            Variant* self = (Variant*) info.This()->GetAlignedPointerFromInternalField(IF_Pointer);
            self->set_keyed(key, value, r_valid);
            if (!r_valid)
            {
                jsb_throw(isolate, "invalid call");
                return;
            }
        }

        static void _get_keyed(const v8::FunctionCallbackInfo<v8::Value>& info)
        {
            v8::Isolate* isolate = info.GetIsolate();
            const v8::Local<v8::Context> context = isolate->GetCurrentContext();
            jsb_check(TypeConvert::is_variant(info.This()));
            if (info.Length() != 1)
            {
                jsb_throw(isolate, "bad params");
                return;
            }
            Variant key;
            if (!TypeConvert::js_to_gd_var(isolate, context, info[0], key))
            {
                jsb_throw(isolate, "bad value");
                return;
            }
            bool r_valid;
            const Variant* self = (Variant*) info.This()->GetAlignedPointerFromInternalField(IF_Pointer);
            const Variant value = self->get_keyed(key, r_valid);
            if (!r_valid)
            {
                jsb_throw(isolate, "invalid key?");
                return;
            }
            v8::Local<v8::Value> r_val;
            if (!TypeConvert::gd_var_to_js(isolate, context, value, r_val))
            {
                jsb_throw(isolate, "bad translation");
                return;
            }
            info.GetReturnValue().Set(r_val);
        }

        template<bool HasReturnValueT>
        static void call_builtin_function(Variant* self, const internal::FBuiltinMethodInfo& method_info,
            const v8::FunctionCallbackInfo<v8::Value>& info, v8::Isolate* isolate, const v8::Local<v8::Context>& context,
            const bool utility = false)
        {
            const int argc = utility ? info.Length() - 1 : info.Length();
            if (!method_info.check_argc(argc))
            {
                jsb_throw(isolate, "num of arguments does not meet the requirement: " + self->stringify() + " - " + Variant::get_type_name(self->get_type()) + "::" + method_info.method_info->method.name);
                return;
            }

            // prepare argv
            const auto &default_arguments = method_info.method_info->method.default_arguments;
            const int known_argc = (int) method_info.argument_types.size();
            const int allocated_argc = MAX(known_argc, argc);
            const Variant** argv = jsb_stackalloc(const Variant*, allocated_argc);
            Variant* args = jsb_stackalloc(Variant, allocated_argc);
            for (int index = 0; index < allocated_argc; ++index)
            {
                memnew_placement(&args[index], Variant);
                argv[index] = &args[index];
                if (index < known_argc)
                {
                    if (index < argc)
                    {
                        if (TypeConvert::js_to_gd_var(isolate, context, info[utility ? index + 1 : index], method_info.argument_types[index], args[index]))
                        {
                            continue;
                        }
                    }
                    else
                    {
                        // identical to: i - p_argcount + (dvs - missing)
                        const int default_index = index - (int) (known_argc - default_arguments.size());
                        if (default_index >= 0)
                        {
                            args[index] = default_arguments[default_index];
                            continue;
                        }
                    }
                }
                else
                {
                    if (TypeConvert::js_to_gd_var(isolate, context, info[utility ? index + 1 : index], args[index]))
                    {
                        continue;
                    }
                }

                // revert all constructors
                const String error_message = jsb_errorf("bad argument: %d", utility ? index + 1 : index);
                while (index >= 0) { args[index--].~Variant(); }
                jsb_throw(isolate, error_message);
                return;
            }

            // call godot method
            if constexpr (HasReturnValueT)
            {
                Variant crval;
                internal::VariantUtil::construct_variant(crval, method_info.return_type);
                method_info.method_info->validated_call(self, argv, allocated_argc, &crval);

                // don't forget to destruct all stack allocated variants
                for (int index = 0; index < allocated_argc; ++index)
                {
                    args[index].~Variant();
                }

                v8::Local<v8::Value> jrval;
                if (TypeConvert::gd_var_to_js(isolate, context, crval, jrval))
                {
                    info.GetReturnValue().Set(jrval);
                    return;
                }
                jsb_throw(isolate, "failed to translate godot variant to v8 value");
            }
            else
            {
                method_info.method_info->validated_call(self, argv, allocated_argc, nullptr);

                // don't forget to destruct all stack allocated variants
                for (int index = 0; index < allocated_argc; ++index)
                {
                    args[index].~Variant();
                }

            }
        }

        template<bool HasReturnValueT>
        static void _instance_method(const v8::FunctionCallbackInfo<v8::Value>& info)
        {
            v8::Isolate* isolate = info.GetIsolate();
            const v8::Local<v8::Context> context = isolate->GetCurrentContext();
            const int method_index = info.Data().As<v8::Int32>()->Value();
            const internal::FBuiltinMethodInfo& method_info = GetVariantInfoCollection(Environment::wrap(context)).methods[method_index];
            Variant* self = TypeConvert::is_variant(info.This())
                ? (Variant*) info.This()->GetAlignedPointerFromInternalField(IF_Pointer)
                : nullptr;
            if (!self)
            {
                jsb_throw(isolate, "no bound this");
                return;
            }

            call_builtin_function<HasReturnValueT>(self, method_info, info, isolate, context);
        }

        template<bool HasReturnValueT>
        static void _static_method(const v8::FunctionCallbackInfo<v8::Value>& info)
        {
            v8::Isolate* isolate = info.GetIsolate();
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            const internal::FBuiltinMethodInfo& method_info = GetVariantInfoCollection(Environment::wrap(context)).methods[info.Data().As<v8::Int32>()->Value()];

            call_builtin_function<HasReturnValueT>(nullptr, method_info, info, isolate, context);
        }

        template<Variant::Type VariantT, bool HasReturnValueT>
        static void _utility_method(const v8::FunctionCallbackInfo<v8::Value>& info)
        {
            v8::Isolate* isolate = info.GetIsolate();

            if (info.Length() < 1 || info[0]->IsObject() || info[0]->IsNull())
            {
                jsb_throw(isolate, "utility methods' first argument must be a JS primitive");
                return;
            }

            const v8::Local<v8::Context> context = isolate->GetCurrentContext();
            const internal::FBuiltinMethodInfo& method_info = GetVariantInfoCollection(Environment::wrap(context)).methods[info.Data().As<v8::Int32>()->Value()];

            Variant variant;
            if (!TypeConvert::js_to_gd_var(isolate, context, info[0], VariantT, variant))
            {
                jsb_throw(isolate, "Failed to convert utility function JS primitive to the required Godot variant type");
                return;
            }

            call_builtin_function<HasReturnValueT>(&variant, method_info, info, isolate, context, true);
        }

        static impl::ClassBuilder get_class_builder(const ClassRegister& p_env, const NativeClassID p_class_id, const StringName& p_class_name)
        {
            JSB_DEFINE_FAST_CONSTRUCTOR(Vector2, p_class_id, p_class_name);
            JSB_DEFINE_FAST_CONSTRUCTOR(Vector2i, p_class_id, p_class_name);
            JSB_DEFINE_FAST_CONSTRUCTOR(Vector3, p_class_id, p_class_name);
            JSB_DEFINE_FAST_CONSTRUCTOR(Vector3i, p_class_id, p_class_name);
            JSB_DEFINE_FAST_CONSTRUCTOR(Vector4, p_class_id, p_class_name);
            JSB_DEFINE_FAST_CONSTRUCTOR(Vector4i, p_class_id, p_class_name);
            JSB_DEFINE_FAST_CONSTRUCTOR(Rect2, p_class_id, p_class_name);
            JSB_DEFINE_FAST_CONSTRUCTOR(Rect2i, p_class_id, p_class_name);

            // fallback
            {
                const uint32_t constructor_index = (uint32_t) GetVariantInfoCollection(p_env.env).constructors.size();
                GetVariantInfoCollection(p_env.env).constructors.append({});
                internal::FConstructorInfo& constructor_info = GetVariantInfoCollection(p_env.env).constructors.write[constructor_index];
                const api_tool::ApiBuiltinClass* api_builtin_class = get_api_info();
                jsb_check(api_builtin_class != nullptr);
                const auto& constructors = api_builtin_class->constructors;
                const int count = (int) constructors.size();
                constructor_info.variants.resize_zeroed(count);
                for (int index = 0; index < count; ++index)
                {
                    internal::FConstructorVariantInfo& variant_info = constructor_info.variants.write[index];
                    variant_info.constructor_info = &constructors[index];
                    const int arg_count = (int) constructors[index].arguments.size();
                    variant_info.argument_types.resize(arg_count);
                    for (int arg_index = 0; arg_index < arg_count; ++arg_index)
                    {
                        variant_info.argument_types.write[arg_index] = constructors[index].arguments[arg_index].type;
                    }
                }
                return impl::ClassBuilder::New<IF_VariantFieldCount>(p_env.isolate,
                    p_class_name,
                    &VariantConstructor<T>::constructor,
                    constructor_index);
            }
        }

        // called in Environment::expose_class
        static NativeClassInfoPtr reflect_bind(const ClassRegister& p_env, NativeClassID* r_class_id = nullptr)
        {
            const StringName& class_name = internal::NamingUtil::get_class_name(p_env.type_name);

            if (class_name != p_env.type_name)
            {
                internal::StringNames::get_singleton().add_replacement(p_env.type_name, class_name);
            }

            const api_tool::ApiBuiltinClass* api_builtin_class = get_api_info();
            jsb_checkf(api_builtin_class, "Failed to find primitive type: " + Variant::get_type_name(TYPE));

            const NativeClassID class_id = p_env->add_native_class(NativeClassType::GodotPrimitive, class_name);
            impl::ClassBuilder class_builder = get_class_builder(p_env, class_id, class_name);

            // properties (getset)
            {
                for (const api_tool::ApiMemberInfo& member : api_builtin_class->members)
                {
                    const StringName& name = member.name;
                    const Variant::Type member_type = member.type;

                    JSB_DEFINE_FAST_GETSET(member_type, real_t, name, &member);
                    JSB_DEFINE_FAST_GETSET(member_type, int32_t, name, &member);
                    // fallback to reflection invocation
                    const int collection_index = (int) GetVariantInfoCollection(p_env.env).primitive_members.size();
                    GetVariantInfoCollection(p_env.env).primitive_members.append({ &member });

                    class_builder.Instance().Property(internal::NamingUtil::get_member_name(name), _getter, _setter, collection_index);
                }
            }

            // indexed accessor
            if (api_builtin_class->has_indexing_return_type)
            {
                class_builder.Instance().Method(internal::NamingUtil::get_member_name("set_indexed"), _set_indexed);
                class_builder.Instance().Method(internal::NamingUtil::get_member_name("get_indexed"), _get_indexed);
            }

            // keyed accessor
            if (api_builtin_class->is_keyed)
            {
                class_builder.Instance().Method(internal::NamingUtil::get_member_name("set_keyed"), _set_keyed);
                class_builder.Instance().Method(internal::NamingUtil::get_member_name("get_keyed"), _get_keyed);
            }

            // methods
            {
                for (const api_tool::ApiBuiltInMethod& method_info : api_builtin_class->methods)
                {
                    const StringName& name = method_info.method.name;
                    const int argument_count = method_info.method.arguments.size();
                    const bool has_return_value = method_info.has_returns();
                    const Variant::Type return_type = (Variant::Type) method_info.method.return_val.type;
                    const String member_name = internal::NamingUtil::get_member_name(name);

#if JSB_FAST_REFLECTION
                    if (method_info.is_vararg())
                    {
                        //TODO hardcoded branches for fast method reflection wrapper
                        if (has_return_value)
                        {
                            if (ReflectBuiltinMethodPointerCall<T, real_t>::is_supported(return_type))
                            {
                                if (argument_count == 0)
                                {
                                    // func: float ();
                                    void* func_ptr = (void*) method_info.get_func_ptr();
                                    if (method_info.is_static())
                                    {
                                        class_builder.Static().Method(member_name,
                                            ReflectBuiltinMethodPointerCall<T, real_t>::template call<false>, func_ptr);
                                    }
                                    else
                                    {
                                        class_builder.Instance().Method(member_name,
                                            ReflectBuiltinMethodPointerCall<T, real_t>::template call<true>, func_ptr);
                                    }
                                    continue;
                                }
                                if (argument_count == 1)
                                {
                                    const Variant::Type arg_type_0 = (Variant::Type) method_info.method.arguments[0].type;
                                    if (arg_type_0 == Variant::FLOAT)
                                    {
                                        // func: float (float);
                                        void* func_ptr = (void*) method_info.get_func_ptr();
                                        if (method_info.is_static())
                                        {
                                            class_builder.Static().Method(member_name,
                                                ReflectBuiltinMethodPointerCall<T, real_t, real_t>::template call<false>, func_ptr);
                                        }
                                        else
                                        {
                                            class_builder.Instance().Method(member_name,
                                                ReflectBuiltinMethodPointerCall<T, real_t, real_t>::template call<true>, func_ptr);
                                        }
                                        continue;
                                    }
                                }
                            }
                            else if (ReflectBuiltinMethodPointerCall<T, int32_t>::is_supported(return_type))
                            {
                                if (argument_count == 0)
                                {
                                    // func: int32 ();
                                    void* func_ptr = (void*) method_info.get_func_ptr();
                                    if (method_info.is_static())
                                    {
                                        class_builder.Static().Method(member_name,
                                            ReflectBuiltinMethodPointerCall<T, int32_t>::template call<false>, func_ptr);
                                    }
                                    else
                                    {
                                        class_builder.Instance().Method(member_name,
                                            ReflectBuiltinMethodPointerCall<T, int32_t>::template call<true>, func_ptr);
                                    }
                                    continue;
                                }
                            }
                            else if (ReflectBuiltinMethodPointerCall<T, bool>::is_supported(return_type))
                            {
                                if (argument_count == 0)
                                {
                                    // func: bool ();
                                    void* func_ptr = (void*) method_info.get_func_ptr();
                                    if (method_info.is_static())
                                    {
                                        class_builder.Static().Method(member_name,
                                            ReflectBuiltinMethodPointerCall<T, bool>::template call<false>, func_ptr);
                                    }
                                    else
                                    {
                                        class_builder.Instance().Method(member_name,
                                            ReflectBuiltinMethodPointerCall<T, bool>::template call<true>, func_ptr);
                                    }
                                    continue;
                                }
                            }
                        }
                        else if (ReflectBuiltinMethodPointerCall<T, void>::is_supported(return_type))
                        {
                            if (argument_count == 0)
                            {
                                // func: void ();
                                void* func_ptr = (void*) method_info.get_func_ptr();
                                if (method_info.is_static())
                                {
                                    class_builder.Static().Method(member_name,
                                        ReflectBuiltinMethodPointerCall<T, void>::template call<false>, func_ptr);
                                }
                                else
                                {
                                    class_builder.Instance().Method(member_name,
                                        ReflectBuiltinMethodPointerCall<T, void>::template call<true>, func_ptr);
                                }
                                continue;
                            }
                            if (argument_count == 1)
                            {
                                const Variant::Type arg_type_0 = (Variant::Type) method_info.method.arguments[0].type;
                                if (arg_type_0 == Variant::FLOAT)
                                {
                                    // func: void (float);
                                    void* func_ptr = (void*) method_info.get_func_ptr();
                                    if (method_info.is_static())
                                    {
                                        class_builder.Static().Method(member_name,
                                            ReflectBuiltinMethodPointerCall<T, void, real_t>::template call<false>, func_ptr);
                                    }
                                    else
                                    {
                                        class_builder.Instance().Method(member_name,
                                            ReflectBuiltinMethodPointerCall<T, void, real_t>::template call<true>, func_ptr);
                                    }
                                    continue;
                                }
                            }
                        }
                    }
#endif

                    // convert method info, and store
                    const int collection_index = (int) GetVariantInfoCollection(p_env.env).methods.size();
                    GetVariantInfoCollection(p_env.env).methods.append({});
                    internal::FBuiltinMethodInfo& method_info_storage = GetVariantInfoCollection(p_env.env).methods.write[collection_index];
                    method_info_storage.set_debug_name(member_name);
                    method_info_storage.method_info = &method_info;
                    method_info_storage.return_type = return_type;
                    method_info_storage.argument_types.resize_zeroed(argument_count);
                    method_info_storage.is_vararg = method_info.is_vararg();
                    for (int argument_index = 0; argument_index < argument_count; ++argument_index)
                    {
                        const Variant::Type type = (Variant::Type) method_info.method.arguments[argument_index].type;
                        method_info_storage.argument_types.write[argument_index] = type;
                    }

                    // function wrapper
                    if (has_return_value)
                    {
                        if (method_info.is_static())
                        {
                            class_builder.Static().Method(member_name, _static_method<true>, collection_index);
                        }
                        else
                        {
                            class_builder.Instance().Method(member_name, _instance_method<true>, collection_index);
                        }
                    }
                    else
                    {
                        if (method_info.is_static())
                        {
                            class_builder.Static().Method(member_name, _static_method<false>, collection_index);
                        }
                        else
                        {
                            class_builder.Instance().Method(member_name, _instance_method<false>, collection_index);
                        }
                    }
                }

                ReflectAdditionalMethodRegister<T>::register_(class_builder);
            }

            // operators
            {
                OperatorRegister<T>::generate(class_builder);
            }

            // enums
            HashSet<StringName> enum_constants;
            {
                for (const api_tool::ApiEnumInfo& enum_info : api_builtin_class->enums)
                {
                    String exposed_enum_name = internal::NamingUtil::get_enum_name(enum_info.name);
                    auto enum_decl = class_builder.Static().Enum(exposed_enum_name);
                    for (const api_tool::ApiEnumValue& enum_value : enum_info.values)
                    {
                        enum_decl.Value(internal::NamingUtil::get_enum_value_name(enum_value.name), enum_value.value);
                        enum_constants.insert(enum_value.name);
                    }
                }
            }

            // constants
            {
                for (const api_tool::ApiBuiltInClassConstantInfo& constant : api_builtin_class->constants)
                {
                    // exclude all enum constants
                    if (enum_constants.has(constant.name)) continue;
                    class_builder.Static().LazyProperty(internal::NamingUtil::get_constant_name(constant.name), _get_constant_value_lazy);
                }
            }

            // special identifier for the convenience to get Variant::Type in scripts
            {
                jsb_check(TYPE >= 0);
                class_builder.Static().Value(jsb_name(p_env, __builtin_type__), (int32_t) TYPE);
            }

            {
                if (r_class_id) *r_class_id = class_id;

                NativeClassInfoPtr class_info = p_env.env->get_native_class(class_id);
                class_info->finalizer = &finalizer;
                class_info->clazz = class_builder.Build();
                jsb_check(!class_info->clazz.IsEmpty());
                return class_info;
            }
        }

        static void utility_noop_constructor(const v8::FunctionCallbackInfo<v8::Value>& _info)
        {
        }

        // Expose primitive instance methods as static utility functions for variant types not exposed to JS e.g. String
        static NativeClassInfoPtr reflect_bind_utilities(const ClassRegister& p_env, NativeClassID* r_class_id = nullptr)
        {
            const StringName& class_name = internal::NamingUtil::get_class_name(p_env.type_name);

            if (class_name != p_env.type_name)
            {
                internal::StringNames::get_singleton().add_replacement(p_env.type_name, class_name);
            }

            const NativeClassID class_id = p_env->add_native_class(NativeClassType::GodotPrimitive, class_name);

            v8::Isolate* isolate = p_env->get_isolate();
            NativeClassInfoPtr class_info = p_env->get_native_class(class_id);
            impl::ClassBuilder class_builder = impl::ClassBuilder::New<0>(isolate, class_info->name, &utility_noop_constructor, *class_id);

            auto static_builder = class_builder.Static();

            const api_tool::ApiBuiltinClass* api_builtin_class = get_api_info();
            jsb_check(api_builtin_class);

            // methods
            for (const api_tool::ApiBuiltInMethod& method_info : api_builtin_class->methods)
            {
                const StringName& name = method_info.method.name;
                const int argument_count = method_info.method.arguments.size();
                const bool has_return_value = method_info.has_returns();
                const Variant::Type return_type = method_info.method.return_val.type;
                String member_name = internal::NamingUtil::get_member_name(name);

                if (member_name == "length")
                {
                    // We can't bind a property named .length to a function in JS because it clashes with a built-in
                    // property.
                    member_name = "length_";
                }

                // convert method info, and store
                const int collection_index = (int) GetVariantInfoCollection(p_env.env).methods.size();
                GetVariantInfoCollection(p_env.env).methods.append({});
                internal::FBuiltinMethodInfo& method_info_storage = GetVariantInfoCollection(p_env.env).methods.write[collection_index];
                method_info_storage.set_debug_name(member_name);
                method_info_storage.method_info = &method_info;
                method_info_storage.return_type = return_type;
                method_info_storage.argument_types.resize_zeroed(argument_count);
                method_info_storage.is_vararg = method_info.is_vararg();
                for (int argument_index = 0; argument_index < argument_count; ++argument_index)
                {
                    const Variant::Type type = method_info.method.arguments[argument_index].type;
                    method_info_storage.argument_types.write[argument_index] = type;
                }

                // function wrapper
                if (has_return_value)
                {
                    if (method_info.is_static())
                    {
                        static_builder.Method(member_name, _static_method<true>, collection_index);
                    }
                    else
                    {
                        static_builder.Method(member_name, _utility_method<TYPE, true>, collection_index);
                    }
                }
                else if (method_info.is_static())
                {
                    static_builder.Method(member_name, _static_method<false>, collection_index);
                }
                else
                {
                    static_builder.Method(member_name, _utility_method<TYPE, false>, collection_index);
                }
            }

            ReflectAdditionalMethodRegister<T>::register_(class_builder);

            // enums
            HashSet<StringName> enum_constants;
            {
                for (const api_tool::ApiEnumInfo& enum_info : api_builtin_class->enums)
                {
                    String exposed_enum_name = internal::NamingUtil::get_enum_name(enum_info.name);
                    auto enum_decl = static_builder.Enum(exposed_enum_name);
                    for (const api_tool::ApiEnumValue& enum_value : enum_info.values)
                    {
                        enum_decl.Value(internal::NamingUtil::get_enum_value_name(enum_value.name), enum_value.value);
                        enum_constants.insert(enum_value.name);
                    }
                }
            }

            // constants
            {
                for (const api_tool::ApiBuiltInClassConstantInfo& constant : api_builtin_class->constants)
                {
                    // exclude all enum constants
                    if (enum_constants.has(constant.name)) continue;
                    static_builder.LazyProperty(internal::NamingUtil::get_constant_name(constant.name), _get_constant_value_lazy);
                }
            }

            {
                if (r_class_id) *r_class_id = class_id;

                NativeClassInfoPtr class_info_result = p_env.env->get_native_class(class_id);
                class_info_result->clazz = class_builder.Build();
                jsb_check(!class_info_result->clazz.IsEmpty());
                return class_info_result;
            }
        }
    };

    void register_primitive_bindings_reflect(Environment* p_env)
    {
#pragma push_macro("DEF")
#   undef   DEF
#   define  DEF(TypeName) p_env->add_class_register(static_cast<Variant::Type>(GetTypeInfo<TypeName>::VARIANT_TYPE), &VariantBind<TypeName>::reflect_bind);
#   include "jsb_primitive_types.def.h"
#pragma pop_macro("DEF")

        p_env->add_class_register(static_cast<Variant::Type>(GetTypeInfo<String>::VARIANT_TYPE), &VariantBind<String>::reflect_bind_utilities);
    }
}

#endif
