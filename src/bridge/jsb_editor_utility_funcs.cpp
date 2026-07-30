#include "jsb_editor_utility_funcs.h"
#include "jsb_type_convert.h"
#include "jsb_environment.h"
#include "../api_tool/api_tool.h"
#include "../api_tool/api_tool_types.h"
#if JSB_WITH_EDITOR_UTILITY_FUNCS
#include "../weaver-editor/jsb_editor_plugin.h"
#endif


#if JSB_WITH_EDITOR_UTILITY_FUNCS
namespace jsb_private
{
    //NOTE dummy functions only for compile-time check and never being really compiled
    template <typename T>                   bool get_member_name(const T&);
    template <typename T>                   bool get_member_name(const volatile T&);
    template <typename R, typename... Args> bool get_member_name(R (*)(Args...));
}

#define JSB_TYPE_BEGIN(InType) template<> struct OperatorRegister<InType>\
    {\
        typedef InType CurrentType;\
        static void generate(const v8::Local<v8::Context>& context, const v8::Local<v8::Array>& operators)\
        {
#define JSB_TYPE_END() \
        }\
    };

#define JSB_DEFINE_OVERLOADED_BINARY_BEGIN(InOperator) OverloadedBinaryOperator(JSB_OPERATOR_NAME(InOperator), context, operators)
#define JSB_DEFINE_BINARY_OVERLOAD(TReturn, TLeft, TRight) .Define<TReturn, TLeft, TRight>()
#define JSB_DEFINE_OVERLOADED_BINARY_END() ;
#define JSB_DEFINE_UNARY(InOperator) UnaryOperator::Define<CurrentType>(context, operators, JSB_OPERATOR_NAME(InOperator));
#define JSB_DEFINE_COMPARATOR(InOperator) Comparator::Define<CurrentType, CurrentType>(context, operators, JSB_OPERATOR_NAME(InOperator));

namespace jsb
{
    namespace
    {
        template<int N>
        void set_field(v8::Isolate* isolate, const v8::Local<v8::Context>& context, const v8::Local<v8::Object>& obj, const char (&field_name)[N], const v8::Local<v8::Value>& field_value)
        {
            obj->Set(context, impl::Helper::new_string(isolate, field_name), field_value).Check();
        }

        template<int N>
        void set_field(v8::Isolate* isolate, const v8::Local<v8::Context>& context, const v8::Local<v8::Object>& obj, const char (&field_name)[N], const StringName& field_value)
        {
            set_field(isolate, context, obj, field_name, impl::Helper::new_string(isolate, field_value));
        }

        template<int N>
        void set_field(v8::Isolate* isolate, const v8::Local<v8::Context>& context, const v8::Local<v8::Object>& obj, const char (&field_name)[N], const int32_t& field_value)
        {
            set_field(isolate, context, obj, field_name, v8::Int32::New(isolate, field_value));
        }

        template<int N>
        void set_field(v8::Isolate* isolate, const v8::Local<v8::Context>& context, const v8::Local<v8::Object>& obj, const char (&field_name)[N], const uint32_t& field_value)
        {
            set_field(isolate, context, obj, field_name, v8::Int32::NewFromUnsigned(isolate, field_value));
        }

        template<int N>
        void set_field(v8::Isolate* isolate, const v8::Local<v8::Context>& context, const v8::Local<v8::Object>& obj, const char (&field_name)[N], const String& field_value)
        {
            set_field(isolate, context, obj, field_name, impl::Helper::new_string(isolate, field_value));
        }

        template<int N>
        void set_field(v8::Isolate* isolate, const v8::Local<v8::Context>& context, const v8::Local<v8::Object>& obj, const char (&field_name)[N], const char*& field_value)
        {
            set_field(isolate, context, obj, field_name, impl::Helper::new_string(isolate, field_value));
        }

        template<int N>
        void set_field(v8::Isolate* isolate, const v8::Local<v8::Context>& context, const v8::Local<v8::Object>& obj, const char (&field_name)[N], const bool& field_value)
        {
            set_field(isolate, context, obj, field_name, v8::Boolean::New(isolate, field_value));
        }

        template<int N>
        void set_field(v8::Isolate* isolate, const v8::Local<v8::Context>& context, const v8::Local<v8::Object>& obj, const char (&field_name)[N], double field_value)
        {
            set_field(isolate, context, obj, field_name, v8::Number::New(isolate, field_value));
        }

        template<int N>
        void set_field(v8::Isolate* isolate, const v8::Local<v8::Context>& context, const v8::Local<v8::Object>& obj, const char (&field_name)[N], int64_t field_value)
        {
            set_field(isolate, context, obj, field_name, impl::Helper::new_integer(isolate, field_value));
        }

        void build_property_info(v8::Isolate* isolate, const v8::Local<v8::Context>& context, const PropertyInfo& property_info, const v8::Local<v8::Object>& object, bool method)
        {
            String name = method
                    ? internal::NamingUtil::get_parameter_name(property_info.name)
                    : internal::NamingUtil::get_member_name(property_info.name);

            const String info_class_name = property_info.class_name;
            String exposed_class_name {};
            if (info_class_name.find(".") >= 0)
            {
                const PackedStringArray components = info_class_name.split(".", false);

                if (components.size() == 2)
                {
                    String class_name = internal::NamingUtil::get_class_name(components[0]);
                    String enum_name = internal::NamingUtil::get_enum_name(components[1]);
                    exposed_class_name = class_name + "." + enum_name;
                }
                else
                {
                    // Should not occur
                    exposed_class_name = property_info.class_name;
                }
            }
            else
            {
                exposed_class_name = internal::NamingUtil::get_class_name(property_info.class_name);
            }

            set_field(isolate, context, object, "name", name);
            set_field(isolate, context, object, "type", property_info.type);
            set_field(isolate, context, object, "class_name", exposed_class_name);
            set_field(isolate, context, object, "hint", property_info.hint);
            set_field(isolate, context, object, "hint_string", property_info.hint_string);
            set_field(isolate, context, object, "usage", property_info.usage);
        }

        void build_property_setget_info(v8::Isolate* isolate, const v8::Local<v8::Context>& context, const api_tool::ApiPropertyInfo &api_prop, const v8::Local<v8::Object>& object)
        {
            set_field(isolate, context, object, "internal_name", api_prop.property.name);
            set_field(isolate, context, object, "name", internal::NamingUtil::get_member_name(api_prop.property.name));
            set_field(isolate, context, object, "type", api_prop.property.type);
            set_field(isolate, context, object, "index", api_prop.index);
            set_field(isolate, context, object, "setter", internal::NamingUtil::get_member_name(api_prop.setter));
            set_field(isolate, context, object, "getter", internal::NamingUtil::get_member_name(api_prop.getter));
        }

        void build_property_default_value(v8::Isolate* isolate, const v8::Local<v8::Context>& context, const Variant& property_value, Variant::Type property_type, const v8::Local<v8::Object>& object)
        {
            v8::Local<v8::Value> val;
            if (property_value.get_type() == Variant::NIL)
            {
                set_field(isolate, context, object, "type", property_type);
                set_field(isolate, context, object, "value", v8::Null(isolate));
                return;
            }
            if (!TypeConvert::gd_var_to_js(isolate, context, property_value, property_type, val))
            {
                JSB_LOG(Error, "unresolved default value");
                return;
            }
            set_field(isolate, context, object, "type", property_type);
            set_field(isolate, context, object, "value", val);
        }

        void build_constructor_info(v8::Isolate* isolate, const v8::Local<v8::Context>& context, const api_tool::ApiConstructorInfo& api_constructor, const v8::Local<v8::Object>& object)
        {
            const int argc = api_constructor.arguments.size();
            v8::Local<v8::Array> args_obj = v8::Array::New(isolate, argc);
            for (int index = 0; index < argc; ++index)
            {
                const PropertyInfo& argument_info = api_constructor.arguments[index];
                v8::Local<v8::Object> arg_obj = v8::Object::New(isolate);
                set_field(isolate, context, arg_obj, "name", internal::NamingUtil::get_parameter_name(argument_info.name));
                set_field(isolate, context, arg_obj, "type", argument_info.type);
                args_obj->Set(context, index, arg_obj).Check();
            }
            set_field(isolate, context, object, "arguments", args_obj);
        }

        void build_method_info(v8::Isolate* isolate, const v8::Local<v8::Context>& context, const MethodInfo& method_info, const v8::Local<v8::Object>& object)
        {
            set_field(isolate, context, object, "internal_name", method_info.name);
            set_field(isolate, context, object, "id", method_info.id);
            set_field(isolate, context, object, "name", internal::NamingUtil::get_member_name(method_info.name));
            set_field(isolate, context, object, "hint_flags", (uint32_t) method_info.flags);
            set_field(isolate, context, object, "is_static", (bool)(method_info.flags & METHOD_FLAG_STATIC));
            set_field(isolate, context, object, "is_const", (bool)(method_info.flags & METHOD_FLAG_CONST));
            set_field(isolate, context, object, "is_vararg", (bool)(method_info.flags & METHOD_FLAG_VARARG));
            set_field(isolate, context, object, "argument_count", method_info.arguments.size());

            // write type info for `return`
            const bool has_return_value =
                    method_info.return_val.type != Variant::NIL
                || (method_info.return_val.usage & PROPERTY_USAGE_NIL_IS_VARIANT);

            if (has_return_value)
            {
                const PropertyInfo& return_info = method_info.return_val;
                v8::Local<v8::Object> property_info_obj = v8::Object::New(isolate);
                build_property_info(isolate, context, return_info, property_info_obj, true);
                set_field(isolate, context, object, "return_", property_info_obj);
            }

            const int argument_num = method_info.arguments.size();
            Vector<Variant::Type> argument_types;

            argument_types.resize(argument_num);
            // write type info for `arguments`
            {
                v8::Local<v8::Array> args_obj = v8::Array::New(isolate, argument_num);
                int index = 0;
                for (auto it = method_info.arguments.begin(); it != method_info.arguments.end(); ++it)
                {
                    jsb_check(index < argument_num);
                    const PropertyInfo& arg_info = *it;
                    v8::Local<v8::Object> property_info_obj = v8::Object::New(isolate);
                    build_property_info(isolate, context, arg_info, property_info_obj, true);
                    argument_types.write[index] = arg_info.type;
                    args_obj->Set(context, index++, property_info_obj).Check();
                }
                set_field(isolate, context, object, "args_", args_obj);
            }

            // write type info for `defaults`
            {
                const int argc = method_info.default_arguments.size();
                v8::Local<v8::Array> args_obj = v8::Array::New(isolate, argc);
                for (int index = 0; index < argc; ++index)
                {
                    v8::Local<v8::Object> property_info_obj = v8::Object::New(isolate);
                    const Variant value = method_info.default_arguments[index];
                    const Variant::Type type = argument_types[argument_types.size() - (argc - index)];
                    build_property_default_value(isolate, context, value, type, property_info_obj);
                    args_obj->Set(context, index, property_info_obj).Check();
                }
                set_field(isolate, context, object, "default_arguments", args_obj);
            }
        }

        void build_enum_info(v8::Isolate* isolate, const v8::Local<v8::Context>& context, const api_tool::ApiEnumInfo &api_enum, const v8::Local<v8::Object>& enum_object)
        {
            v8::Local<v8::Object> literals_obj = v8::Object::New(isolate);
            for (const auto &E : api_enum.values)
            {
                const String &name = E.name;
                const int value = E.value;
                literals_obj->Set(context, impl::Helper::new_string(isolate, name), v8::Number::New(isolate, value)).Check();
            }
            set_field(isolate, context, enum_object, "name", internal::NamingUtil::get_enum_name(api_enum.name));
            set_field(isolate, context, enum_object, "literals", literals_obj);
            set_field(isolate, context, enum_object, "is_bitfield", api_enum.is_bitfield);
        }

        void build_signal_info(v8::Isolate* isolate, const v8::Local<v8::Context>& context, const api_tool::ApiSignalInfo& signal_info, const v8::Local<v8::Object>& signal_obj)
        {
            set_field(isolate, context, signal_obj, "internal_name", signal_info.name);
            set_field(isolate, context, signal_obj, "name", internal::NamingUtil::get_member_name(signal_info.name));
            v8::Local<v8::Object> method_obj = v8::Object::New(isolate);

            godot::MethodInfo method_info;
            method_info.name = signal_info.name;
            method_info.arguments = signal_info.arguments;
            build_method_info(isolate, context, method_info, method_obj);
            set_field(isolate, context, signal_obj, "method_", method_obj);
        }

        void build_builtin_class_constant(v8::Isolate *isolate, const v8::Local<v8::Context> &context, const api_tool::ApiBuiltInClassConstantInfo &api_builtin_class_constant, const v8::Local<v8::Object> &builtin_class_constant_obj)
        {
            set_field(isolate, context, builtin_class_constant_obj, "name", api_builtin_class_constant.name);
            set_field(isolate, context, builtin_class_constant_obj, "type", api_builtin_class_constant.type);
            const Variant constant_value = api_builtin_class_constant.value;
            // TODO: 所有类型！
            switch (api_builtin_class_constant.type)
            {
            case Variant::BOOL: set_field(isolate, context, builtin_class_constant_obj, "value", (bool) constant_value); break;
            case Variant::INT: set_field(isolate, context, builtin_class_constant_obj, "value", (int64_t) constant_value); break;
            case Variant::FLOAT: set_field(isolate, context, builtin_class_constant_obj, "value", (double) constant_value); break;
            default: break;
            }
        }

        v8::Local<v8::Object> build_class_info(v8::Isolate* isolate, const v8::Local<v8::Context>& context, const StringName& class_name, const HashSet<StringName>* class_rpc_methods)
        {
            v8::Local<v8::Object> class_info_obj = v8::Object::New(isolate);

            if (!api_tool::has_class(class_name)) {
                return class_info_obj;
            }
            const api_tool::ApiClass* api_class = api_tool::find_class(class_name);
            jsb_check(api_class);

            set_field(isolate, context, class_info_obj, "name", internal::NamingUtil::get_class_name(api_class->name));
            set_field(isolate, context, class_info_obj, "internal_name", api_class->name);
            set_field(isolate, context, class_info_obj, "super", api_class->inherits);

            // set_field(isolate, context, class_info_obj, "is_refcounted", api_class->is_refcounted);
            // set_field(isolate, context, class_info_obj, "is_instantiable", api_class->is_instantiable);

#if JSB_EXCLUDE_GETSET_METHODS
            HashSet<StringName> omitted_methods;
#endif
            // class: properties
            {
                JSB_HANDLE_SCOPE(isolate);
                v8::Local<v8::Array> properties_obj = v8::Array::New(isolate, (int) api_class->properties.size());
                set_field(isolate, context, class_info_obj, "properties", properties_obj);
                int index = 0;
                for (const auto& api_prop : api_class->properties) {
                    JSB_HANDLE_SCOPE(isolate);
                    v8::Local<v8::Object> property_info_obj = v8::Object::New(isolate);
                    v8::Local<v8::Object> property_setget_info_obj = v8::Object::New(isolate);
                    set_field(isolate, context, property_setget_info_obj, "info", property_info_obj);
                    build_property_info(isolate, context, api_prop.property, property_info_obj, false);
                    build_property_setget_info(isolate, context, api_prop, property_setget_info_obj);

                    properties_obj->Set(context, index++, property_setget_info_obj).Check();
#if JSB_EXCLUDE_GETSET_METHODS
                    // 仅非 index 访问的属性其访问器能够被后续忽略。
                    if (api_prop.index >= 0) {
                        if (internal::VariantUtil::is_valid_name(api_prop.getter)) omitted_methods.insert(api_prop.getter);
                        if (internal::VariantUtil::is_valid_name(api_prop.setter)) omitted_methods.insert(api_prop.setter);
                    }
#endif
                }
            }

            // class: methods/ rpc methods / virtual methods
            {
                JSB_HANDLE_SCOPE(isolate);
#if JSB_EXCLUDE_GETSET_METHODS
                constexpr int len = 0;
#else
                const int len = (int) api_class->methods.size();
#endif
                const v8::Local<v8::Array> rpc_methods_obj = v8::Array::New(isolate);
                const v8::Local<v8::Array> virtual_methods_obj = v8::Array::New(isolate); // Virtual 
                const v8::Local<v8::Array> methods_obj = v8::Array::New(isolate, len); // 非 Virtual
                set_field(isolate, context, class_info_obj, "rpc_methods", rpc_methods_obj);
                set_field(isolate, context, class_info_obj, "virtual_methods", virtual_methods_obj);
                set_field(isolate, context, class_info_obj, "methods", methods_obj);
                int rpc_methods_index = 0;
                int virtual_methods_index = 0;
                int methods_index = 0;

                for (const auto& api_method : api_class->methods) {
                    const godot::MethodInfo& method_info = api_method.method;
                    uint32_t flags = method_info.flags;
                    bool is_rpc = class_rpc_methods && ((flags & METHOD_FLAG_STATIC) == 0) && (class_rpc_methods->has(method_info.name) || class_rpc_methods->has(internal::NamingUtil::get_member_name(method_info.name)));
                    bool is_virtual = api_method.is_virtual();
#if JSB_EXCLUDE_GETSET_METHODS
                    if (!is_rpc && !is_virtual && omitted_methods.has(api_method.method.name)) continue;
#endif
                    JSB_HANDLE_SCOPE(isolate);
                    v8::Local<v8::Object> method_info_obj = v8::Object::New(isolate);
                    build_method_info(isolate, context, method_info, method_info_obj);
                    if (is_rpc) rpc_methods_obj->Set(context, rpc_methods_index++, method_info_obj).Check();

                    if (is_virtual) virtual_methods_obj->Set(context, virtual_methods_index++, method_info_obj).Check();
                    else
                    {
#if JSB_EXCLUDE_GETSET_METHODS
                        if (omitted_methods.has(api_method.method.name)) continue;
#endif
                        methods_obj->Set(context, methods_index++, method_info_obj).Check();
                    }
                }
            }

            // class: enums
            {
                JSB_HANDLE_SCOPE(isolate);
                v8::Local<v8::Array> enums_obj = v8::Array::New(isolate, (int) api_class->enums.size());
                set_field(isolate, context, class_info_obj, "enums", enums_obj);
                int index = 0;
                for (const auto& api_enum : api_class->enums) {
                    JSB_HANDLE_SCOPE(isolate);
                    v8::Local<v8::Object> enum_info_obj = v8::Object::New(isolate);
                    build_enum_info(isolate, context, api_enum, enum_info_obj);
                    enums_obj->Set(context, index++, enum_info_obj).Check();
                }
            }

            // class: constants (int only)
            {
                JSB_HANDLE_SCOPE(isolate);
                v8::Local<v8::Array> constants_obj = v8::Array::New(isolate, (int) api_class->constants.size());
                set_field(isolate, context, class_info_obj, "constants", constants_obj);
                int index = 0;
                for (const auto& constant : api_class->constants) {
                    JSB_HANDLE_SCOPE(isolate);
                    v8::Local<v8::Object> constant_info_obj = v8::Object::New(isolate);
                    set_field(isolate, context, constant_info_obj, "name", internal::NamingUtil::get_constant_name(constant.name));
                    set_field(isolate, context, constant_info_obj, "value", constant.value);
                    constants_obj->Set(context, index++, constant_info_obj).Check();
                }
            }

            // class: signals
            {
                JSB_HANDLE_SCOPE(isolate);
                v8::Local<v8::Array> signals_obj = v8::Array::New(isolate, (int) api_class->signals.size());
                set_field(isolate, context, class_info_obj, "signals", signals_obj);
                int index = 0;
                for (const auto& api_signal : api_class->signals) {
                    JSB_HANDLE_SCOPE(isolate);
                    v8::Local<v8::Object> signal_info_obj = v8::Object::New(isolate);
                    build_signal_info(isolate, context, api_signal, signal_info_obj);
                    signals_obj->Set(context, index++, signal_info_obj).Check();
                }
            }

            return class_info_obj;
        }
    }

    struct OverloadedBinaryOperator
    {
        String op_name;
        const v8::Local<v8::Context>& context;
        const v8::Local<v8::Array>& operators;

        OverloadedBinaryOperator(const String& p_name, const v8::Local<v8::Context>& p_context, const v8::Local<v8::Array>& p_operators)
        : op_name(p_name), context(p_context), operators(p_operators) {}

        template<typename TReturn, typename TLeft, typename TRight>
        OverloadedBinaryOperator& Define()
        {
            JSB_HANDLE_SCOPE(context->GetIsolate());

            v8::Local<v8::Object> obj = v8::Object::New(context->GetIsolate());

            set_field(context->GetIsolate(), context, obj, "name", op_name);
            set_field(context->GetIsolate(), context, obj, "return_type", (int) GetTypeInfo<TReturn>::VARIANT_TYPE);
            set_field(context->GetIsolate(), context, obj, "left_type", (int) GetTypeInfo<TLeft>::VARIANT_TYPE);
            set_field(context->GetIsolate(), context, obj, "right_type", (int) GetTypeInfo<TRight>::VARIANT_TYPE);
            const uint32_t len = operators->Length();
            operators->Set(context, len, obj).Check();
            return *this;
        }
    };

    struct UnaryOperator
    {
        template<typename TypeName>
        static void Define(const v8::Local<v8::Context>& context, const v8::Local<v8::Array>& operators, const String& op_name)
        {
            JSB_HANDLE_SCOPE(context->GetIsolate());

            v8::Local<v8::Object> obj = v8::Object::New(context->GetIsolate());

            set_field(context->GetIsolate(), context, obj, "name", op_name);
            set_field(context->GetIsolate(), context, obj, "return_type", (int) GetTypeInfo<TypeName>::VARIANT_TYPE);
            set_field(context->GetIsolate(), context, obj, "left_type", (int) GetTypeInfo<TypeName>::VARIANT_TYPE);
            set_field(context->GetIsolate(), context, obj, "right_type", (int) Variant::NIL);
            const uint32_t len = operators->Length();
            operators->Set(context, len, obj).Check();
        }
    };

    struct Comparator
    {
        template<typename TLeft, typename TRight>
        static void Define(const v8::Local<v8::Context>& context, const v8::Local<v8::Array>& operators, const String& op_name)
        {
            JSB_HANDLE_SCOPE(context->GetIsolate());

            v8::Local<v8::Object> obj = v8::Object::New(context->GetIsolate());

            set_field(context->GetIsolate(), context, obj, "name", op_name);
            set_field(context->GetIsolate(), context, obj, "return_type", (int) GetTypeInfo<bool>::VARIANT_TYPE);
            set_field(context->GetIsolate(), context, obj, "left_type", (int) GetTypeInfo<TLeft>::VARIANT_TYPE);
            set_field(context->GetIsolate(), context, obj, "right_type", (int) GetTypeInfo<TRight>::VARIANT_TYPE);
            const uint32_t len = operators->Length();
            operators->Set(context, len, obj).Check();
        }
    };

    template<typename TypeName>
    struct OperatorRegister
    {
        static void generate(const v8::Local<v8::Context>& context, const v8::Local<v8::Array>& operators) {}
    };

    #define Number double
    #include "../internal/jsb_primitive_operators.def.h"
    #undef Number

    template<typename T>
    static v8::Local<v8::Value> generate_primitive_type(v8::Isolate* isolate, const v8::Local<v8::Context>& context)
    {
        constexpr static Variant::Type TYPE = (Variant::Type)GetTypeInfo<T>::VARIANT_TYPE;
        const StringName type_name = Variant::get_type_name(TYPE);
        const api_tool::ApiBuiltinClass* builtin_class = api_tool::find_builtin_class(type_name);
        jsb_check(builtin_class);

        v8::Local<v8::Object> class_info_obj = v8::Object::New(isolate);
        set_field(isolate, context, class_info_obj, "name", internal::NamingUtil::get_class_name(type_name));
        set_field(isolate, context, class_info_obj, "type", TYPE);

        if (builtin_class->has_indexing_return_type) {
            set_field(isolate, context, class_info_obj, "element_type", builtin_class->indexing_type);
        }
        set_field(isolate, context, class_info_obj, "is_keyed", builtin_class->is_keyed);

        // constructors
        {
            JSB_HANDLE_SCOPE(isolate);

            v8::Local<v8::Array> constructors_obj = v8::Array::New(isolate, (int) builtin_class->constructors.size());
            set_field(isolate, context, class_info_obj, "constructors", constructors_obj);
            int constructor_index = 0;
            for (const auto& api_constructor: builtin_class->constructors)
            {
                JSB_HANDLE_SCOPE(isolate);
                v8::Local<v8::Object> constructor_obj = v8::Object::New(isolate);
                build_constructor_info(isolate, context, api_constructor, constructor_obj);
                constructors_obj->Set(context, constructor_index++, constructor_obj).Check();
            }
        }

        // properties (getset)
        {
            JSB_HANDLE_SCOPE(isolate);

            v8::Local<v8::Array> members_obj = v8::Array::New(isolate, (int) builtin_class->members.size());
            set_field(isolate, context, class_info_obj, "properties", members_obj);
            int index = 0;
            for (const auto& api_member : builtin_class->members) {
                JSB_HANDLE_SCOPE(isolate);
                v8::Local<v8::Object> member_object = v8::Object::New(isolate);
                set_field(isolate, context, member_object, "name", internal::NamingUtil::get_member_name(api_member.name));
                set_field(isolate, context, member_object, "type", api_member.type);
                members_obj->Set(context, index++, member_object).Check();
            }
        }

        // methods
        {
            JSB_HANDLE_SCOPE(isolate);

            v8::Local<v8::Array> methods_obj = v8::Array::New(isolate, (int) builtin_class->methods.size());
            set_field(isolate, context, class_info_obj, "methods", methods_obj);
            int index = 0;
            for (const auto& api_method : builtin_class->methods)
            {
                JSB_HANDLE_SCOPE(isolate);
                const godot::MethodInfo& method_info = api_method.method;
                v8::Local<v8::Object> method_info_obj = v8::Object::New(isolate);
                build_method_info(isolate, context, method_info, method_info_obj);
                methods_obj->Set(context, index++, method_info_obj).Check();
            }
        }

        // operators
        {
            JSB_HANDLE_SCOPE(isolate);

            v8::Local<v8::Array> operators_obj = v8::Array::New(isolate, (int) builtin_class->operators.size());
            set_field(isolate, context, class_info_obj, "operators", operators_obj);
            int index = 0;
            for (const auto& op : builtin_class->operators) {
                JSB_HANDLE_SCOPE(isolate);
                v8::Local<v8::Object> obj = v8::Object::New(isolate);
                set_field(isolate, context, obj, "name", api_tool::get_variant_operator_name(op.op));
                set_field(isolate, context, obj, "return_type", (int) op.return_type);
                set_field(isolate, context, obj, "left_type", (int) op.left_type);
                set_field(isolate, context, obj, "right_type", (int) op.right_type);
                operators_obj->Set(context, index++, obj).Check();
            }
        }

        // enums
        {
            JSB_HANDLE_SCOPE(isolate);

            v8::Local<v8::Array> enums_obj = v8::Array::New(isolate, (int) builtin_class->enums.size());
            set_field(isolate, context, class_info_obj, "enums", enums_obj);
            int index = 0;
            for (const auto& api_enum : builtin_class->enums)
            {
                JSB_HANDLE_SCOPE(isolate);
                v8::Local<v8::Object> enum_info_obj = v8::Object::New(isolate);
                build_enum_info(isolate, context, api_enum, enum_info_obj);
                enums_obj->Set(context, index++, enum_info_obj).Check();
            }
        }

        // constants
        {
            JSB_HANDLE_SCOPE(isolate);

            v8::Local<v8::Array> constants_obj = v8::Array::New(isolate, (int) builtin_class->constants.size());
            set_field(isolate, context, class_info_obj, "constants", constants_obj);
            int index = 0;
            for (const auto& api_builtin_class_constant : builtin_class->constants)
            {
                JSB_HANDLE_SCOPE(isolate);
                v8::Local<v8::Object> constant_info_obj = v8::Object::New(isolate);
                build_builtin_class_constant(isolate, context, api_builtin_class_constant, constant_info_obj);
                constants_obj->Set(context, index++, constant_info_obj).Check();
            }
        }

        return class_info_obj;
    }


    template<typename T>
    static v8::Local<v8::Value> generate_primitive_type_utilities(v8::Isolate* isolate, const v8::Local<v8::Context>& context)
    {
        constexpr static Variant::Type TYPE = (Variant::Type)GetTypeInfo<T>::VARIANT_TYPE;
        const StringName type_name = Variant::get_type_name(TYPE);
        const api_tool::ApiBuiltinClass* builtin_class = api_tool::find_builtin_class(type_name);
        jsb_check(builtin_class);

        v8::Local<v8::Object> class_info_obj = v8::Object::New(isolate);
        String class_name = internal::NamingUtil::get_class_name(type_name);
        set_field(isolate, context, class_info_obj, "name", class_name);
        set_field(isolate, context, class_info_obj, "type", TYPE);
        
        if (builtin_class->has_indexing_return_type) {
            set_field(isolate, context, class_info_obj, "element_type", builtin_class->indexing_type);
        }
        set_field(isolate, context, class_info_obj, "is_keyed", builtin_class->is_keyed);

        // methods (static/utility variants, with implicit `target` param for non-static methods)
        {
            JSB_HANDLE_SCOPE(isolate);

            v8::Local<v8::Array> methods_obj = v8::Array::New(isolate);
            set_field(isolate, context, class_info_obj, "methods", methods_obj);
            int index = 0;
            for (const auto& api_method : builtin_class->methods)
            {
                JSB_HANDLE_SCOPE(isolate);
                const godot::MethodInfo& ori_method_info = api_method.method;

                godot::MethodInfo method_info;
                
                method_info.name = ori_method_info.name;
                method_info.flags = METHOD_FLAG_STATIC | ori_method_info.flags; // 强制为 Static 附加 VARARG 与 CONST

                method_info.return_val.type = ori_method_info.return_val.type;
                method_info.return_val.usage = ori_method_info.return_val.usage;

                if (ori_method_info.flags & METHOD_FLAG_STATIC)
                {
                    PropertyInfo prop_info;
                    prop_info.name = "target";
                    prop_info.type = TYPE;
                    method_info.arguments.push_back(prop_info);
                }

                for (const godot::PropertyInfo &arg :ori_method_info.arguments)
                {
                    PropertyInfo prop_info;
                    prop_info.name = arg.name;
                    prop_info.type = arg.type;
                    method_info.arguments.push_back(prop_info);
                }
                method_info.default_arguments = ori_method_info.default_arguments;

                v8::Local<v8::Object> method_info_obj = v8::Object::New(isolate);
                build_method_info(isolate, context, method_info, method_info_obj);
                methods_obj->Set(context, index++, method_info_obj).Check();
            }
        }

        // enums
        {
            JSB_HANDLE_SCOPE(isolate);

            v8::Local<v8::Array> enums_obj = v8::Array::New(isolate, (int) builtin_class->enums.size());
            set_field(isolate, context, class_info_obj, "enums", enums_obj);
            int index = 0;
            for (const auto& api_enum : builtin_class->enums)
            {
                JSB_HANDLE_SCOPE(isolate);
                v8::Local<v8::Object> enum_info_obj = v8::Object::New(isolate);
                build_enum_info(isolate, context, api_enum, enum_info_obj);
                enums_obj->Set(context, index++, enum_info_obj).Check();
            }
        }

        // constants
        {
            JSB_HANDLE_SCOPE(isolate);

            v8::Local<v8::Array> constants_obj = v8::Array::New(isolate, (int) builtin_class->constants.size());
            set_field(isolate, context, class_info_obj, "constants", constants_obj);
            int index = 0;
            for (const auto& api_builtin_class_constant : builtin_class->constants)
            {
                JSB_HANDLE_SCOPE(isolate);
                v8::Local<v8::Object> constant_info_obj = v8::Object::New(isolate);
                build_builtin_class_constant(isolate, context, api_builtin_class_constant, constant_info_obj);
                constants_obj->Set(context, index++, constant_info_obj).Check();
            }
        }

        return class_info_obj;
    }

    // TODO: 生成 @GlobalScope
    static void _get_class_doc(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        v8::Isolate* isolate = info.GetIsolate();
        v8::HandleScope handle_scope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        const String name = impl::Helper::to_string(isolate, info[0]);
        const String original_name = internal::StringNames::get_singleton().get_original_name(name);

        JSB_HANDLE_SCOPE(isolate);
        if (auto doc = api_tool::find_document(original_name)) {
            v8::Local<v8::Object> class_doc_obj = v8::Object::New(isolate);
            // set_field(isolate, context, class_doc_obj, "name", doc->name);
            // set_field(isolate, context, class_doc_obj, "description", doc->description);
set_field(isolate, context, class_doc_obj, "brief_description", doc->brief_description);

            {
                // doc:constants
                JSB_HANDLE_SCOPE(isolate);

                v8::Local<v8::Object> constants_obj = v8::Object::New(isolate);
                set_field(isolate, context, class_doc_obj, "constants", constants_obj);
                for (const auto& constant_doc : doc->constants)
                {
                    JSB_HANDLE_SCOPE(isolate);
                    v8::Local<v8::Object> constant_obj = v8::Object::New(isolate);
                    const String constant_name = internal::NamingUtil::get_constant_name(constant_doc.name);
                    constants_obj->Set(context, impl::Helper::new_string(isolate, constant_name), constant_obj).Check();

                    set_field(isolate, context, constant_obj, "description", constant_doc.description);
                }

                // doc:enums。内置类遵循 godot 的文档格式，整合进 constants 中
                for (const auto& enum_doc : doc->enums) {
                    for (const auto& enum_value_doc : enum_doc.values) {
                        JSB_HANDLE_SCOPE(isolate);
                        v8::Local<v8::Object> constant_obj = v8::Object::New(isolate);

                        const String constant_name = internal::NamingUtil::get_constant_name(enum_value_doc.name);
                        v8::Local<v8::Name> js_constant_name = impl::Helper::new_string(isolate, constant_name);
                        constants_obj->Set(context, js_constant_name, constant_obj).Check();
                        set_field(isolate, context, constant_obj, "description", enum_value_doc.description);
                    }
                }
            }
            // doc:methods
            {
                JSB_HANDLE_SCOPE(isolate);
                v8::Local<v8::Object> methods_obj = v8::Object::New(isolate);
                set_field(isolate, context, class_doc_obj, "methods", methods_obj);
                for (const auto& method_doc : doc->methods) {
                    JSB_HANDLE_SCOPE(isolate);
                    v8::Local<v8::Object> method_obj = v8::Object::New(isolate);
                    const String method_name = internal::NamingUtil::get_member_name(method_doc.name);
                    methods_obj->Set(context, impl::Helper::new_string(isolate, method_name), method_obj).Check();
                    // set_field(isolate, context, method_obj, "name", method_doc.name);
                    set_field(isolate, context, method_obj, "description", method_doc.description);
                }
            }

            // doc:properties
            {
                JSB_HANDLE_SCOPE(isolate);
                v8::Local<v8::Object> properties_obj = v8::Object::New(isolate);
                set_field(isolate, context, class_doc_obj, "properties", properties_obj);
                for (const auto& property_doc : doc->properties) {
                    JSB_HANDLE_SCOPE(isolate);
                    v8::Local<v8::Object> property_obj = v8::Object::New(isolate);
                    const String property_name = internal::NamingUtil::get_member_name(property_doc.name);
                    properties_obj->Set(context, impl::Helper::new_string(isolate, property_name), property_obj).Check();
                    // set_field(isolate, context, property_obj, "name", internal::NamingUtil::get_member_name(property_doc.name));
                    set_field(isolate, context, property_obj, "description", property_doc.description);
                }
            }

            // doc:signals
            {
                JSB_HANDLE_SCOPE(isolate);
                v8::Local<v8::Object> signals_obj = v8::Object::New(isolate);
                set_field(isolate, context, class_doc_obj, "signals", signals_obj);
                for (const auto& signal_doc : doc->signals) {
                    JSB_HANDLE_SCOPE(isolate);
                    v8::Local<v8::Object> signal_obj = v8::Object::New(isolate);
                    const String signal_name = internal::NamingUtil::get_member_name(signal_doc.name);
                    set_field(isolate, context, signal_obj, "name", signal_doc.name);
                    // set_field(isolate, context, signal_obj, "description", signal_doc.description);
                    signals_obj->Set(context, impl::Helper::new_string(isolate, signal_name), signal_obj).Check();
                }
            }

            info.GetReturnValue().Set(class_doc_obj);
        }
    }

    // TODO: 添加 @GlobalScope
    static void _get_classes(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        v8::Isolate* isolate = info.GetIsolate();

        v8::HandleScope handle_scope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        Environment* environment = Environment::wrap(isolate);

        List<StringName> exposed_class_list = internal::NamingUtil::get_exposed_original_class_list();
        HashMap<StringName, HashSet<StringName>> rpc_method_map;

        for (auto& script_class_info : environment->get_script_classes())
        {
            if (script_class_info.rpc_config.is_empty())
            {
                continue;
            }

            HashSet<StringName>& methods = rpc_method_map[script_class_info.js_class_name];

            Array keys = script_class_info.rpc_config.keys();
            for (int i = 0; i < keys.size(); i++)
            {
                methods.insert(keys[i]);
            }
        }

        v8::Local<v8::Array> array = v8::Array::New(isolate, exposed_class_list.size());
        int index = 0;

        for (auto& class_name : exposed_class_list)
        {
            JSB_HANDLE_SCOPE(isolate);
            array->Set(context, index++, build_class_info(isolate, context, class_name, rpc_method_map.getptr(class_name))).Check();
        }

        info.GetReturnValue().Set(array);
    }

    static void _get_global_constants(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        v8::Isolate* isolate = info.GetIsolate();
        v8::HandleScope handle_scope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        // Get list of global constant names from api_tool
        const auto constant_names = api_tool::list_global_constants();
        const auto enum_names = api_tool::list_global_enums();

        // Estimate array size: constants + enums
        v8::Local<v8::Array> array = v8::Array::New(isolate, constant_names.size() + enum_names.size());
        int array_index = 0;

        // First, add individual constants (those not part of an enum)
        for (const StringName& constant_name : constant_names) {
            const auto api_global_constant = api_tool::find_global_constant(constant_name);
            jsb_check(api_global_constant);
            JSB_HANDLE_SCOPE(isolate);
            v8::Local<v8::Object> constant_obj = v8::Object::New(isolate);
            set_field(isolate, context, constant_obj, "name", internal::NamingUtil::get_enum_value_name(constant_name));
            set_field(isolate, context, constant_obj, "value", api_global_constant->value);
            array->Set(context, array_index++, constant_obj).Check();
        }

        // Then, add enums with their values
        for (const StringName& enum_name : enum_names) {
            const auto* enum_info = api_tool::find_global_enum(enum_name);
            jsb_check(enum_info);
            JSB_HANDLE_SCOPE(isolate);
            v8::Local<v8::Object> enum_obj = v8::Object::New(isolate);
            v8::Local<v8::Object> values_obj = v8::Object::New(isolate);
            const StringName exposed_enum_name = internal::NamingUtil::get_enum_name(enum_name);
            set_field(isolate, context, enum_obj, "name", exposed_enum_name);
            set_field(isolate, context, enum_obj, "values", values_obj);
            for (const auto& api_enum : enum_info->values) {
                JSB_HANDLE_SCOPE(isolate);
                values_obj->Set(context,
                    impl::Helper::new_string(isolate, internal::NamingUtil::get_enum_value_name(api_enum.name)),
                    impl::Helper::new_integer(isolate, api_enum.value)).Check();
            }
            array->Set(context, array_index++, enum_obj).Check();
        }

        info.GetReturnValue().Set(array);
    }

    static void _get_primitive_types(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        v8::Isolate* isolate = info.GetIsolate();
        v8::HandleScope handle_scope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        int index = 0;
        v8::Local<v8::Array> array = v8::Array::New(isolate);

#pragma push_macro("DEF")
#   undef   DEF
#   define  DEF(TypeName) { JSB_HANDLE_SCOPE(isolate); array->Set(context, index++, generate_primitive_type<TypeName>(isolate, context)).Check(); }
#   include "jsb_primitive_types.def.h"
#pragma pop_macro("DEF")

        {
            JSB_HANDLE_SCOPE(isolate);
            array->Set(context, index++, generate_primitive_type_utilities<String>(isolate, context)).Check();
        }

        info.GetReturnValue().Set(array);
    }

    static void _get_input_actions(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        v8::Isolate* isolate = info.GetIsolate();
        v8::HandleScope handle_scope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::Local<v8::Array> actions = v8::Array::New(isolate);
        int index = 0;

        TypedArray<Dictionary> property_list = ProjectSettings::get_singleton()->get_property_list();

        for (int i = 0; i < property_list.size(); i++)
        {
            Dictionary property = property_list[i];
            String name = property["name"];
            if (!name.begins_with("input/"))
            {
                continue;
            }

            name = name.substr(name.find("/") + 1, name.length());
            actions->Set(context, index++,  impl::Helper::new_string(isolate, name)).Check();
        }

        info.GetReturnValue().Set(actions);
    }

    static void _get_utility_functions(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        v8::Isolate* isolate = info.GetIsolate();
        v8::HandleScope handle_scope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        const auto utility_function_names = api_tool::list_utility_functions();
        v8::Local<v8::Array> array = v8::Array::New(isolate, utility_function_names.size());
        int index = 0;
        for (const StringName& name : utility_function_names)
        {
            if (auto utility_func = api_tool::find_utility_function(name)) {
                JSB_HANDLE_SCOPE(isolate);
                const MethodInfo& method_info = utility_func->method;
                v8::Local<v8::Object> method_info_obj = v8::Object::New(isolate);
                build_method_info(isolate, context, method_info, method_info_obj);
                array->Set(context, index++, method_info_obj).Check();
            }
        }
        info.GetReturnValue().Set(array);
    }

    static void _get_singletons(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        v8::Isolate* isolate = info.GetIsolate();
        v8::HandleScope handle_scope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        Engine* engine = Engine::get_singleton();
        PackedStringArray singleton_list = engine->get_singleton_list();
        v8::Local<v8::Array> array = v8::Array::New(isolate, singleton_list.size());
        for (int i = 0; i < singleton_list.size(); i++)
        {
            JSB_HANDLE_SCOPE(isolate);
            StringName singleton_name = singleton_list[i];
            Object* singleton = engine->get_singleton(singleton_name);
            v8::Local<v8::Object> constant_obj = v8::Object::New(isolate);
            const StringName& class_name = singleton->get_class();

            // if (!internal::VariantUtil::is_valid_name(singleton.class_name))
            // {
            //     singleton.class_name = class_name;
            //     JSB_LOG(Verbose, "singleton (%s) has a hidden class_name, restoring with '%s'", singleton_name, class_name);
            // }

            set_field(isolate, context, constant_obj, "name", internal::NamingUtil::get_class_name(singleton_name));
            set_field(isolate, context, constant_obj, "class_name", internal::NamingUtil::get_class_name(singleton->get_class()));
            // set_field(isolate, context, constant_obj, "user_created", singleton.user_created); // TODO: 应该不需要，如果不需要的话对应移除 .d.ts 中的字段
            // set_field(isolate, context, constant_obj, "editor_only", singleton.editor_only); // TODO: 应该不需要，如果不需要的话对应移除 .d.ts 中的字段
            array->Set(context, i, constant_obj).Check();
        }
        info.GetReturnValue().Set(array);
    }

    static void _delete_file(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        v8::Isolate* isolate = info.GetIsolate();
        v8::HandleScope handle_scope(isolate);

        if (info.Length() != 1 || !info[0]->IsString())
        {
            jsb_throw(isolate, "bad path");
            return;
        }
        internal::PathUtil::delete_file(impl::Helper::to_string(isolate, info[0]));
    }

    static void _install_project_files(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        v8::Isolate* isolate = info.GetIsolate();
        GodotJSEditorPlugin* editor_plugin = GodotJSEditorPlugin::get_singleton();

        if (editor_plugin == nullptr)
        {
            jsb_throw(isolate, "editor plugin unavailable");
            return;
        }

        v8::HandleScope handle_scope(isolate);

        auto context = isolate->GetCurrentContext();
        auto result = v8::Promise::Resolver::New(context);

        if (result.IsEmpty())
        {
            jsb_throw(isolate, "Failed to setup promise");
            return;
        }

        auto resolver = result.ToLocalChecked();

        bool force = info.Length() >= 0 && info[0]->IsBoolean() && info[0].As<v8::Boolean>()->Value();
        editor_plugin->try_install_project_files([&](auto success)
        {
            if (success)
            {
                resolver->Resolve(context, v8::Undefined(isolate));
            }
            else
            {
                resolver->Reject(context, impl::Helper::new_string_ascii(isolate, "Failed to install project files"));
            }
        }, force);

        info.GetReturnValue().Set(resolver->GetPromise());
    }

    static void _install_static_types(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        v8::Isolate* isolate = info.GetIsolate();
        GodotJSEditorPlugin* editor_plugin = GodotJSEditorPlugin::get_singleton();

        if (editor_plugin == nullptr)
        {
            jsb_throw(isolate, "editor plugin unavailable");
            return;
        }

        v8::HandleScope handle_scope(isolate);

        auto context = isolate->GetCurrentContext();
        auto result = v8::Promise::Resolver::New(context);

        if (result.IsEmpty())
        {
            jsb_throw(isolate, "Failed to setup promise");
            return;
        }

        auto resolver = result.ToLocalChecked();
        editor_plugin->install_static_types([&](auto success)
        {
            if (success)
            {
                resolver->Resolve(context, v8::Undefined(isolate));
            }
            else
            {
                resolver->Reject(context, impl::Helper::new_string_ascii(isolate, "Failed to install static types"));
            }
        });

        info.GetReturnValue().Set(resolver->GetPromise());
    }

    static void _generate_types(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        v8::Isolate* isolate = info.GetIsolate();

        GodotJSEditorPlugin* editor_plugin = GodotJSEditorPlugin::get_singleton();

        if (editor_plugin == nullptr)
        {
            jsb_throw(isolate, "editor plugin unavailable");
            return;
        }

        v8::HandleScope handle_scope(isolate);

        auto context = isolate->GetCurrentContext();
        auto result = v8::Promise::Resolver::New(context);

        if (result.IsEmpty())
        {
            jsb_throw(isolate, "Failed to setup promise");
            return;
        }

        auto resolver = result.ToLocalChecked();

        bool skip_static_types = info.Length() >= 0 && info[0]->IsBoolean() && info[0].As<v8::Boolean>()->Value();
        editor_plugin->generate_types([&](auto success)
        {
            if (success)
            {
                resolver->Resolve(context, v8::Undefined(isolate));
            }
            else
            {
                resolver->Reject(context, impl::Helper::new_string_ascii(isolate, "Failed to generate types"));
            }
        }, skip_static_types);

        info.GetReturnValue().Set(resolver->GetPromise());
    }

    static void _cleanup_invalid_files(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        v8::Isolate* isolate = info.GetIsolate();

        GodotJSEditorPlugin* editor_plugin = GodotJSEditorPlugin::get_singleton();

        if (editor_plugin == nullptr)
        {
            jsb_throw(isolate, "editor plugin unavailable");
            return;
        }

        v8::HandleScope handle_scope(isolate);

        auto context = isolate->GetCurrentContext();
        auto result = v8::Promise::Resolver::New(context);

        if (result.IsEmpty())
        {
            jsb_throw(isolate, "Failed to setup promise");
            return;
        }

        auto resolver = result.ToLocalChecked();

        editor_plugin->cleanup_invalid_files([&](auto success)
        {
            if (success)
            {
                resolver->Resolve(context, v8::Undefined(isolate));
            }
            else
            {
                resolver->Reject(context, impl::Helper::new_string_ascii(isolate, "Failed to cleanup invalid files"));
            }
        });

        info.GetReturnValue().Set(resolver->GetPromise());
    }

    void EditorUtilityFuncs::expose(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> jsb_obj)
    {
        v8::Local<v8::Object> editor_obj = v8::Object::New(isolate);

        jsb_obj->Set(context, impl::Helper::new_string_ascii(isolate, "editor"), editor_obj).Check();
        editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "get_class_doc"), JSB_NEW_FUNCTION(context, _get_class_doc, {})).Check();
        editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "get_classes"), JSB_NEW_FUNCTION(context, _get_classes, {})).Check();
        editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "get_global_constants"), JSB_NEW_FUNCTION(context, _get_global_constants, {})).Check();
        editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "get_singletons"), JSB_NEW_FUNCTION(context, _get_singletons, {})).Check();
        editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "get_utility_functions"), JSB_NEW_FUNCTION(context, _get_utility_functions, {})).Check();
        editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "get_primitive_types"), JSB_NEW_FUNCTION(context, _get_primitive_types, {})).Check();
        editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "get_input_actions"), JSB_NEW_FUNCTION(context, _get_input_actions, {})).Check();
        editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "delete_file"), JSB_NEW_FUNCTION(context, _delete_file, {})).Check();
        editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "install_project_files"), JSB_NEW_FUNCTION(context, _install_project_files, {})).Check();
        editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "install_static_types"), JSB_NEW_FUNCTION(context, _install_static_types, {})).Check();
        editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "generate_types"), JSB_NEW_FUNCTION(context, _generate_types, {})).Check();
        editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "cleanup_invalid_files"), JSB_NEW_FUNCTION(context, _cleanup_invalid_files, {})).Check();
        editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "VERSION_DOCS_URL"), impl::Helper::new_string(isolate, "https://docs.godotengine.org/en/latest")).Check(); // TODO: 版本链接拼接
    }
}
#else
namespace jsb
{
    namespace
    {
        static void _editor_only(const v8::FunctionCallbackInfo<v8::Value>& info)
        {
            jsb_throw(info.GetIsolate(), "jsb.editor methods are only available in editor builds");
        }
    }

    void EditorUtilityFuncs::expose(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> jsb_obj)
    {
        v8::Local<v8::Object> editor_obj = v8::Object::New(isolate);
        v8::Local<v8::Function> editor_only = JSB_NEW_FUNCTION(context, _editor_only, {});

        jsb_obj->Set(context, impl::Helper::new_string_ascii(isolate, "editor"), editor_obj).Check();
        editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "get_class_doc"), editor_only).Check();
        editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "get_classes"), editor_only).Check();
        editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "get_global_constants"), editor_only).Check();
        editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "get_singletons"), editor_only).Check();
        editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "get_utility_functions"), editor_only).Check();
        editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "get_primitive_types"), editor_only).Check();
        editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "get_input_actions"), editor_only).Check();
        editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "delete_file"), editor_only).Check();
        editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "install_project_files"), editor_only).Check();
        editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "install_static_types"), editor_only).Check();
        editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "generate_types"), editor_only).Check();
        editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "cleanup_invalid_files"), editor_only).Check();
        editor_obj->Set(context, impl::Helper::new_string_ascii(isolate, "VERSION_DOCS_URL"), impl::Helper::new_string_ascii(isolate, "")).Check();
    }
}
#endif // endif JSB_WITH_EDITOR_UTILITY_FUNCS
