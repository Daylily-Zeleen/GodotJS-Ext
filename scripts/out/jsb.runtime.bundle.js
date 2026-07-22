"use strict";
define("godot.lib.api", ["require", "exports"], function (require, exports) {
    "use strict";
    const godot_api = require("godot");
    const jsb_api = require("godot-jsb");
    const ProxyTarget = godot_api.ProxyTarget;
    const { get_class, get_enum, get_enum_value, get_internal_mapping, get_member } = jsb_api.internal.names;
    function pascal_to_upper_snake_case(str) {
        return str.replace(/[a-z][A-Z]|[0-9][A-Z][a-z]/g, (m) => `${m[0]}_${m.slice(1)}`).toUpperCase();
    }
    function is_basic_object(value) {
        const proto = Object.getPrototypeOf(value);
        return proto === Object.prototype || !proto;
    }
    function proxy_unwrap_value(value) {
        return value?.[ProxyTarget] ?? value;
    }
    function proxy_wrap_value(value) {
        if (value == null) {
            return value;
        }
        if (value[ProxyTarget]) {
            return value;
        }
        if (typeof value === "function") {
            return function_proxy(value);
        }
        if (typeof value !== "object") {
            return value;
        }
        if (Array.isArray(value)) {
            return array_proxy(value);
        }
        const constructor = value.constructor;
        if (constructor != null && constructor === globalThis[constructor.name]) {
            // JS built-in
            return value;
        }
        return is_basic_object(value) ? object_proxy(value) : instance_proxy(value);
    }
    const object_handler = {
        get(target, p, _receiver) {
            if (p === ProxyTarget) {
                return target;
            }
            const value = Reflect.get(target, p);
            const descriptor = Object.getOwnPropertyDescriptor(target, p);
            if (typeof p !== "string" || (descriptor && !descriptor.writable && !descriptor.configurable)) {
                return value;
            }
            if (p[0]?.toUpperCase() === p[0] &&
                value &&
                typeof value === "object" &&
                is_basic_object(value) &&
                !Object.entries(value).find(([k, v]) => k[0].toUpperCase() !== k[0] || typeof v !== "number")) {
                return enum_proxy(value);
            }
            return proxy_wrap_value(value);
        },
        getOwnPropertyDescriptor(target, p) {
            return (Reflect.getOwnPropertyDescriptor(target, typeof p === "string" ? get_member(p) : p) ??
                Reflect.getOwnPropertyDescriptor(target, p));
        },
        has(target, p) {
            return Reflect.has(target, typeof p === "string" ? get_member(p) : p) || Reflect.has(target, p);
        },
        ownKeys(target) {
            return Reflect.ownKeys(target).map((key) => (typeof key === "string" && get_internal_mapping(key)) || key);
        },
        set(target, p, new_value, _receiver) {
            return Reflect.set(target, typeof p === "string" ? get_member(p) : p, new_value);
        },
    };
    const object_properties_handler = {
        ...object_handler,
        get(target, p, _receiver) {
            if (p === ProxyTarget) {
                return target;
            }
            const descriptor = Object.getOwnPropertyDescriptor(target, p);
            if (typeof p !== "string" || (descriptor && !descriptor.writable && !descriptor.configurable)) {
                return Reflect.get(target, p);
            }
            const value = Reflect.get(target, p !== "toString" ? get_member(p) : p);
            if (p[0]?.toUpperCase() === p[0] &&
                value &&
                typeof value === "object" &&
                is_basic_object(value) &&
                !Object.entries(value).find(([k, v]) => k[0].toUpperCase() !== k[0] || typeof v !== "number")) {
                return enum_proxy(value);
            }
            return proxy_wrap_value(value);
        },
    };
    function array_proxy(arr) {
        return new Proxy(arr, object_handler);
    }
    function object_proxy(obj, remap_properties) {
        return new Proxy(obj, remap_properties ? object_properties_handler : object_handler);
    }
    const key_only_handler = {
        apply: function (target, this_arg, args) {
            return Reflect.apply(target, this_arg?.[ProxyTarget] ?? this_arg, args);
        },
        get(target, p, _receiver) {
            if (p === ProxyTarget) {
                return target;
            }
            const descriptor = Object.getOwnPropertyDescriptor(target, p);
            if (typeof p !== "string" || (descriptor && !descriptor.writable && !descriptor.configurable)) {
                return Reflect.get(target, p);
            }
            return key_only_proxy(Reflect.get(target, p !== "toString" ? get_member(p) : p));
        },
    };
    function key_only_proxy(target) {
        return new Proxy(target, key_only_handler);
    }
    const instance_handler = {
        defineProperty() {
            return false;
        },
        deleteProperty() {
            return false;
        },
        get(target, p, _receiver) {
            if (p === ProxyTarget) {
                return target;
            }
            if (typeof p !== "string" || p === "constructor") {
                return Reflect.get(target, p);
            }
            return proxy_wrap_value(Reflect.get(target, p !== "toString" ? get_member(p) : p));
        },
        getOwnPropertyDescriptor(target, p) {
            return Reflect.getOwnPropertyDescriptor(target, typeof p === "string" ? get_member(p) : p);
        },
        has(target, p) {
            return Reflect.has(target, typeof p === "string" ? get_member(p) : p);
        },
        isExtensible() {
            return false;
        },
        ownKeys(target) {
            return Reflect.ownKeys(target).map((key) => (typeof key === "string" && get_internal_mapping(key)) || key);
        },
        preventExtensions() {
            return true;
        },
        set(target, p, new_value, _receiver) {
            return Reflect.set(target, typeof p === "string" ? get_member(p) : p, new_value);
        },
        setPrototypeOf(_target) {
            return false;
        },
    };
    function instance_proxy(target_instance) {
        return new Proxy(target_instance, instance_handler);
    }
    const class_handler = {
        ...instance_handler,
        construct(target, args, _new_target) {
            return instance_proxy(new target(...args));
        },
        get(target, p, _receiver) {
            if (p === ProxyTarget) {
                return target;
            }
            if (p === Symbol.hasInstance) {
                return Function[Symbol.hasInstance].bind(target);
            }
            if (typeof p !== "string") {
                return Reflect.get(target, p);
            }
            const descriptor = Object.getOwnPropertyDescriptor(target, p);
            if (descriptor && !descriptor.writable && !descriptor.configurable) {
                return Reflect.get(target, p);
            }
            if (p === "prototype") {
                const proto = Reflect.get(target, "prototype");
                return proto && class_proxy(proto);
            }
            // Preserve native JS class/function stringification behavior.
            // Mapping to Godot `to_string` here can bind an invalid receiver.
            if (p === "toString") {
                return Reflect.get(target, p);
            }
            if (p[0]?.toUpperCase() !== p[0]) {
                return proxy_wrap_value(Reflect.get(target, get_member(p)));
            }
            if (p.toUpperCase() === p) {
                return proxy_wrap_value(Reflect.get(target, p));
            }
            return enum_proxy(Reflect.get(target, get_enum(p)));
        },
    };
    function class_proxy(target_class) {
        return new Proxy(target_class, class_handler);
    }
    const function_handler = {
        ...class_handler,
        apply: function (target, this_arg, args) {
            return proxy_wrap_value(Reflect.apply(target, this_arg?.[ProxyTarget] ?? this_arg, args.map(proxy_wrap_value)));
        },
    };
    function function_proxy(fn) {
        return new Proxy(fn, function_handler);
    }
    const enum_handler = {
        defineProperty() {
            return false;
        },
        deleteProperty() {
            return false;
        },
        get(target, p, _receiver) {
            if (p === ProxyTarget) {
                return target;
            }
            if (typeof p !== "string") {
                return Reflect.get(target, p);
            }
            return Reflect.get(target, get_enum_value(p));
        },
        getOwnPropertyDescriptor(target, p) {
            return Reflect.getOwnPropertyDescriptor(target, typeof p === "string" ? get_enum_value(p) : p);
        },
        has(target, p) {
            return Reflect.has(target, typeof p === "string" ? get_enum_value(p) : p);
        },
        isExtensible() {
            return false;
        },
        ownKeys(target) {
            return Reflect.ownKeys(target).map((key) => (typeof key === "string" ? pascal_to_upper_snake_case(key) : key));
        },
        preventExtensions() {
            return true;
        },
        set() {
            return false;
        },
        setPrototypeOf() {
            return false;
        },
    };
    function enum_proxy(target_enum) {
        if (typeof target_enum !== "object") {
            return target_enum;
        }
        return new Proxy(target_enum, enum_handler);
    }
    const api_handler = (target) => ({
        defineProperty() {
            return false;
        },
        deleteProperty() {
            return false;
        },
        get(_pseudo_target, p, _receiver) {
            if (p === ProxyTarget) {
                return target;
            }
            if (p in _pseudo_target) {
                return _pseudo_target[p];
            }
            if (typeof p !== "string") {
                return Reflect.get(target, p);
            }
            if (p === "toString") {
                return proxy_wrap_value(Reflect.get(target, p));
            }
            // Special case, see jsb_godot_module_loader.cpp
            if (p === "Variant") {
                return object_proxy(Reflect.get(target, p));
            }
            if (p[0]?.toUpperCase() !== p[0]) {
                return proxy_wrap_value(Reflect.get(target, get_member(p)));
            }
            const value = Reflect.get(target, get_class(p));
            if (typeof value === "function") {
                return class_proxy(value);
            }
            if (value == null || typeof value !== "object") {
                return value;
            }
            return is_basic_object(value) ? enum_proxy(value) : instance_proxy(value);
        },
        getOwnPropertyDescriptor(_pseudo_target, p) {
            if (p in _pseudo_target) {
                return Reflect.getOwnPropertyDescriptor(_pseudo_target, p);
            }
            return Reflect.getOwnPropertyDescriptor(target, typeof p === "string" ? get_member(p) : p);
        },
        has(_pseudo_target, p) {
            return Reflect.has(target, typeof p === "string" ? get_class(p) : p) || Reflect.has(_pseudo_target, p);
        },
        isExtensible() {
            return false;
        },
        ownKeys(_pseudo_target) {
            return Reflect.ownKeys(target)
                .map((key) => (typeof key === "string" && get_internal_mapping(key)) || key)
                .concat(Reflect.ownKeys(_pseudo_target));
        },
        preventExtensions() {
            return true;
        },
        set(_pseudo_target, p, new_value, _receiver) {
            return Reflect.set(target, typeof p === "string" ? get_member(p) : p, new_value);
        },
        setPrototypeOf(_pseudo_target) {
            return false;
        },
    });
    const proxy = {
        array_proxy,
        class_proxy,
        enum_proxy,
        function_proxy,
        instance_proxy,
        key_only_proxy,
        object_proxy,
        proxy_unwrap_value,
        proxy_wrap_value,
    };
    const jsb = object_proxy(jsb_api);
    const api = new Proxy({
        jsb,
        proxy,
    }, api_handler(godot_api));
    return api;
});
define("godot.annotations", ["require", "exports", "godot.lib.api"], function (require, exports, lib_api) {
    "use strict";
    Object.defineProperty(exports, "__esModule", { value: true });
    exports.Help = exports.Experimental = exports.Deprecated = exports.Icon = exports.Tool = exports.OnReady = exports.Rpc = exports.ExportFlags = exports.ExportEnum = exports.ExportVar = exports.ExportObject = exports.ExportDictionary = exports.ExportArray = exports.ExportExpEasing = exports.ExportGlobalDir = exports.ExportGlobalFile = exports.ExportFile = exports.ExportIntRange = exports.ExportRange = exports.ExportMultiline = exports.ExportSignal = void 0;
    exports.EnumType = EnumType;
    exports.TypePair = TypePair;
    exports.signal = signal;
    exports.export_multiline = export_multiline;
    exports.export_range = export_range;
    exports.export_range_i = export_range_i;
    exports.export_file = export_file;
    exports.export_dir = export_dir;
    exports.export_global_file = export_global_file;
    exports.export_global_dir = export_global_dir;
    exports.export_exp_easing = export_exp_easing;
    exports.export_array = export_array;
    exports.export_dictionary = export_dictionary;
    exports.export_object = export_object;
    exports.export_ = export_;
    exports.Export = Export;
    exports.export_var = export_var;
    exports.export_enum = export_enum;
    exports.export_flags = export_flags;
    exports.rpc = rpc;
    exports.onready = onready;
    exports.tool = tool;
    exports.icon = icon;
    exports.deprecated = deprecated;
    exports.experimental = experimental;
    exports.help = help;
    exports.createClassBinder = createClassBinder;
    const { jsb, proxy, FloatType, IntegerType, Node, PropertyHint, PropertyUsageFlags, ProxyTarget, Resource, Variant } = lib_api;
    function legacy_decorators_check(context) {
        if (typeof context === "object") {
            throw new Error(`Legacy decorators must be built with experimentalDecorators enabled. Use createClassBinder() instead.`);
        }
    }
    function guess_type_name(type) {
        if (typeof type === "function") {
            return type.name;
        }
        if (type && typeof type === "object") {
            if (typeof type.constructor === "function") {
                return type.constructor.name;
            }
            let proto = Object.getPrototypeOf(type);
            if (typeof proto === "object") {
                return guess_type_name(proto);
            }
        }
        return type;
    }
    function resolve_variant_type(enum_name) {
        const direct = Reflect.get(Variant.Type, enum_name);
        if (typeof direct === "number") {
            return direct;
        }
        const remapped_name = jsb.internal.names.get_enum_value(enum_name);
        const remapped = Reflect.get(Variant.Type, remapped_name);
        if (typeof remapped === "number") {
            return remapped;
        }
        throw new Error(`Unknown Variant.Type enum value: ${enum_name}`);
    }
    const VariantTypeObject = resolve_variant_type("TYPE_OBJECT");
    function invoke_with_this(fn, this_arg, ...args) {
        return Function.prototype.call.call(fn, this_arg, ...args);
    }
    class EnumPlaceholderImpl {
        target;
        constructor(target) {
            this.target = target;
        }
    }
    class TypePairPlaceholderImpl {
        key;
        value;
        constructor(key, value) {
            this.key = key;
            this.value = value;
        }
    }
    function EnumType(type) {
        return new EnumPlaceholderImpl(type);
    }
    function TypePair(key, value) {
        return new TypePairPlaceholderImpl(key, value);
    }
    /** @deprecated Use createClassBinder() instead. */
    function signal() {
        return function (target, name) {
            legacy_decorators_check(name);
            if (typeof name !== "string") {
                throw new Error("Signals must have a string name");
            }
            jsb.internal.add_script_signal(target, name);
        };
    }
    /** @deprecated Use createClassBinder() instead. */
    exports.ExportSignal = signal;
    /** @deprecated Use createClassBinder() instead. */
    function export_multiline() {
        return export_(Variant.Type.TYPE_STRING, { hint: PropertyHint.PROPERTY_HINT_MULTILINE_TEXT });
    }
    /** @deprecated Use createClassBinder() instead. */
    exports.ExportMultiline = export_multiline;
    function __export_range(type, min, max, step = 1, ...extra_hints) {
        let hint_string = `${min},${max},${step}`;
        if (typeof extra_hints !== "undefined") {
            hint_string += "," + extra_hints.join(",");
        }
        return export_(type, { hint: PropertyHint.PROPERTY_HINT_RANGE, hint_string });
    }
    /** @deprecated Use createClassBinder() instead. */
    function export_range(min, max, step = 1, ...extra_hints) {
        return __export_range(Variant.Type.TYPE_FLOAT, min, max, step, ...extra_hints);
    }
    /** @deprecated Use createClassBinder() instead. */
    exports.ExportRange = export_range;
    /** @deprecated Use createClassBinder() instead. */
    function export_range_i(min, max, step = 1, ...extra_hints) {
        return __export_range(Variant.Type.TYPE_INT, min, max, step, ...extra_hints);
    }
    /** @deprecated Use createClassBinder() instead. */
    exports.ExportIntRange = export_range_i;
    /** String as a path to a file, custom filter provided as hint. */
    /** @deprecated Use createClassBinder() instead. */
    function export_file(filter) {
        return export_(Variant.Type.TYPE_STRING, { hint: PropertyHint.PROPERTY_HINT_FILE, hint_string: filter });
    }
    /** @deprecated Use createClassBinder() instead. */
    exports.ExportFile = export_file;
    /** @deprecated Use createClassBinder() instead. */
    function export_dir(filter) {
        return export_(Variant.Type.TYPE_STRING, { hint: PropertyHint.PROPERTY_HINT_DIR, hint_string: filter });
    }
    /** @deprecated Use createClassBinder() instead. */
    function export_global_file(filter) {
        return export_(Variant.Type.TYPE_STRING, { hint: PropertyHint.PROPERTY_HINT_GLOBAL_FILE, hint_string: filter });
    }
    /** @deprecated Use createClassBinder() instead. */
    exports.ExportGlobalFile = export_global_file;
    /** @deprecated Use createClassBinder() instead. */
    function export_global_dir(filter) {
        return export_(Variant.Type.TYPE_STRING, { hint: PropertyHint.PROPERTY_HINT_GLOBAL_DIR, hint_string: filter });
    }
    /** @deprecated Use createClassBinder() instead. */
    exports.ExportGlobalDir = export_global_dir;
    /** @deprecated Use createClassBinder() instead. */
    function export_exp_easing(hint) {
        return export_(Variant.Type.TYPE_FLOAT, { hint: PropertyHint.PROPERTY_HINT_EXP_EASING, hint_string: hint });
    }
    // TODO: Godot's property hints make for a poor API. We should provide convenience methods to build them.
    /** @deprecated Use createClassBinder() instead. */
    exports.ExportExpEasing = export_exp_easing;
    /**
     * A Shortcut for `export_(Variant.Type.TYPE_ARRAY, { class_: clazz })`
     */
    /** @deprecated Use createClassBinder() instead. */
    function export_array(clazz) {
        return export_(Variant.Type.TYPE_ARRAY, { class_: clazz });
    }
    /** @deprecated Use createClassBinder() instead. */
    exports.ExportArray = export_array;
    /**
     * A Shortcut for exporting a dictionary { class_: [key_class, value_class] })`
     */
    /** @deprecated Use createClassBinder() instead. */
    function export_dictionary(key_class, value_class) {
        return export_(Variant.Type.TYPE_DICTIONARY, { class_: TypePair(key_class, value_class) });
    }
    /** @deprecated Use createClassBinder() instead. */
    exports.ExportDictionary = export_dictionary;
    function get_hint_string_for_enum(enum_type) {
        const enum_vs = [];
        for (const [key, value] of Object.entries(enum_type)) {
            if (typeof value === "number" && value >= 0 && Number.isInteger(value)) {
                enum_vs.push(`${key}:${value}`);
            }
        }
        return enum_vs.join(",");
    }
    function get_hint_string(clazz) {
        if (typeof clazz === "symbol") {
            if (clazz === IntegerType) {
                return Variant.Type.TYPE_INT + ":";
            }
            if (clazz === FloatType) {
                return Variant.Type.TYPE_FLOAT + ":";
            }
        }
        if (typeof clazz === "function") {
            const prototype = clazz.prototype;
            if (prototype instanceof Resource) {
                return `${VariantTypeObject}/${PropertyHint.PROPERTY_HINT_RESOURCE_TYPE}:${clazz.name}`;
            }
            else if (prototype instanceof Node ||
                (clazz[ProxyTarget] ?? clazz) === (Node[ProxyTarget] ?? Node)) {
                return `${VariantTypeObject}/${PropertyHint.PROPERTY_HINT_NODE_TYPE}:${clazz.name}`;
            }
            else if (typeof prototype !== "undefined") {
                // other than Resource and Node, only primitive types and enum types are supported in gdscript
                //TODO but we barely know anything about the enum types and int/float/StringName/... in JS
                if (clazz === Boolean) {
                    return Variant.Type.TYPE_BOOL + ":";
                }
                else if (clazz === Number) {
                    // we can only guess the type is float
                    return Variant.Type.TYPE_FLOAT + ":";
                }
                else if (clazz === String) {
                    return Variant.Type.TYPE_STRING + ":";
                }
                else {
                    if (typeof clazz.__builtin_type__ === "number") {
                        return clazz.__builtin_type__ + ":";
                    }
                    else {
                        throw new Error("the given parameters are not supported or not implemented");
                    }
                }
            }
        }
        if (typeof clazz === "object") {
            if (clazz instanceof EnumPlaceholderImpl) {
                return `${Variant.Type.TYPE_INT}/${Variant.Type.TYPE_INT}:${get_hint_string_for_enum(clazz.target)}`;
            }
            // probably an Array (as key-value type descriptor for a Dictionary)
            if (clazz instanceof TypePairPlaceholderImpl) {
                // special case for dictionary, int is preferred for key type of a dictionary
                const key_type = clazz.key === Number ? Variant.Type.TYPE_INT + ":" : get_hint_string(clazz.key);
                const value_type = get_hint_string(clazz.value);
                if (key_type.length === 0 || value_type.length === 0) {
                    throw new Error("the given parameters are not supported or not implemented");
                }
                return key_type + ";" + value_type;
            }
        }
        return "";
    }
    /** @deprecated Use createClassBinder() instead. */
    function export_object(clazz) {
        return export_(VariantTypeObject, { class_: clazz });
    }
    /** @deprecated Use createClassBinder() instead. */
    exports.ExportObject = export_object;
    /**
     * [low level export]
     * @deprecated Use createClassBinder() instead.
     * */
    function export_(type, details) {
        return function (target, name) {
            legacy_decorators_check(name);
            if (typeof name !== "string") {
                throw new Error("Only properties with a string name/key can be exported");
            }
            const ebd = {
                name,
                type: type,
                hint: PropertyHint.PROPERTY_HINT_NONE,
                hint_string: "",
                usage: PropertyUsageFlags.PROPERTY_USAGE_DEFAULT,
            };
            if (typeof details === "object") {
                if (typeof details.hint === "number")
                    ebd.hint = details.hint;
                if (typeof details.usage === "number")
                    ebd.usage = details.usage;
                if (typeof details.hint_string === "string")
                    ebd.hint_string = details.hint_string;
                // overwrite hint if class_ is provided
                try {
                    //TODO more general and unified way to handle all types
                    if (type === VariantTypeObject) {
                        const clazz = details.class_;
                        if (typeof clazz === "function") {
                            const prototype = clazz.prototype;
                            if (prototype instanceof Resource) {
                                ebd.hint = PropertyHint.PROPERTY_HINT_RESOURCE_TYPE;
                                ebd.hint_string = clazz.name;
                                ebd.usage |= PropertyUsageFlags.PROPERTY_USAGE_SCRIPT_VARIABLE;
                            }
                            else if (prototype instanceof Node ||
                                (clazz[ProxyTarget] ?? clazz) === (Node[ProxyTarget] ?? Node)) {
                                ebd.hint = PropertyHint.PROPERTY_HINT_NODE_TYPE;
                                ebd.hint_string = clazz.name;
                                ebd.usage |= PropertyUsageFlags.PROPERTY_USAGE_SCRIPT_VARIABLE;
                            }
                        }
                        jsb.internal.add_script_property(target, ebd);
                        return;
                    }
                    let hint_string = get_hint_string(details.class_);
                    if (hint_string.length > 0) {
                        ebd.hint =
                            type === Variant.Type.TYPE_ARRAY
                                ? PropertyHint.PROPERTY_HINT_ARRAY_TYPE
                                : PropertyHint.PROPERTY_HINT_TYPE_STRING;
                        ebd.hint_string = hint_string;
                        ebd.usage |= PropertyUsageFlags.PROPERTY_USAGE_SCRIPT_VARIABLE;
                    }
                }
                catch (e) {
                    if (ebd.hint === PropertyHint.PROPERTY_HINT_NONE) {
                        console.warn("the given parameters are not supported or not implemented (you need to give hint/hint_string/usage manually)", `class:${guess_type_name(Object.getPrototypeOf(target))} prop:${name} type:${type} class_:${guess_type_name(details.class_)}`);
                    }
                }
            }
            jsb.internal.add_script_property(target, ebd);
        };
    }
    /** @deprecated Use createClassBinder() instead. */
    function Export(type, details) {
        const { hintString, class: cls, ...consistent } = details ?? {};
        return export_(type, {
            ...consistent,
            hint_string: hintString,
            class_: cls,
        });
    }
    /**
     * In Godot, class members can be exported.
     * This means their value gets saved along with the resource (such as the scene) they're attached to.
     * They will also be available for editing in the property editor.
     * Exporting is done by using the `@export_var` (or `@export_`) annotation.
     */
    /** @deprecated Use createClassBinder() instead. */
    function export_var(type, details) {
        return export_(type, details);
    }
    /** @deprecated Use createClassBinder() instead. */
    exports.ExportVar = export_var;
    /**
     * NOTE only int value enums are allowed
     */
    /** @deprecated Use createClassBinder() instead. */
    function export_enum(enum_type) {
        return function (target, name) {
            legacy_decorators_check(name);
            if (typeof name !== "string") {
                throw new Error("Only properties with a string name/key can be exported");
            }
            jsb.internal.add_script_property(target, {
                name,
                type: Variant.Type.TYPE_INT,
                hint: PropertyHint.PROPERTY_HINT_ENUM,
                hint_string: get_hint_string_for_enum(enum_type),
                usage: PropertyUsageFlags.PROPERTY_USAGE_DEFAULT,
            });
        };
    }
    /** @deprecated Use createClassBinder() instead. */
    exports.ExportEnum = export_enum;
    /**
     * NOTE only int value enums are allowed
     */
    /** @deprecated Use createClassBinder() instead. */
    function export_flags(enum_type) {
        return function (target, name) {
            legacy_decorators_check(name);
            if (typeof name !== "string") {
                throw new Error("Only properties with a string name/key can be exported");
            }
            const enum_vs = [];
            for (let c in enum_type) {
                const v = enum_type[c];
                if (typeof v === "string" && enum_type[v] != 0) {
                    enum_vs.push(v + ":" + c);
                }
            }
            const ebd = {
                name,
                type: Variant.Type.TYPE_INT,
                hint: PropertyHint.PROPERTY_HINT_FLAGS,
                hint_string: enum_vs.join(","),
                usage: PropertyUsageFlags.PROPERTY_USAGE_DEFAULT,
            };
            jsb.internal.add_script_property(target, ebd);
        };
    }
    /** @deprecated Use createClassBinder() instead. */
    exports.ExportFlags = export_flags;
    /** @deprecated Use createClassBinder() instead. */
    function rpc(config) {
        return function (target, name) {
            legacy_decorators_check(name);
            if (typeof name !== "string") {
                throw new Error("Only methods with a string name can be registered for RPC");
            }
            if (typeof config !== "undefined") {
                jsb.internal.add_script_rpc(target, name, {
                    rpc_mode: config.mode,
                    call_local: typeof config.sync !== "undefined" ? config.sync == "call_local" : undefined,
                    transfer_mode: config.transfer_mode,
                    channel: config.transfer_channel,
                });
            }
            else {
                jsb.internal.add_script_rpc(target, name, {});
            }
        };
    }
    /** @deprecated Use createClassBinder() instead. */
    exports.Rpc = rpc;
    /**
     * auto initialized on ready (before _ready called)
     *
     * @deprecated Use createClassBinder() instead.
     */
    function onready(evaluator) {
        return function (target, name) {
            legacy_decorators_check(name);
            if (typeof name !== "string") {
                throw new Error("Only methods with a string name can be registered as an onready callback");
            }
            const ebd = { name, evaluator: evaluator };
            jsb.internal.add_script_ready(target, ebd);
        };
    }
    /** @deprecated Use createClassBinder() instead. */
    exports.OnReady = onready;
    /** @deprecated Use createClassBinder() instead. */
    function tool() {
        return function (target, name) {
            legacy_decorators_check(name);
            jsb.internal.add_script_tool(target);
        };
    }
    /** @deprecated Use createClassBinder() instead. */
    exports.Tool = tool;
    /** @deprecated Use createClassBinder() instead. */
    function icon(path) {
        return function (target, name) {
            legacy_decorators_check(name);
            jsb.internal.add_script_icon(target, path);
        };
    }
    /** @deprecated Use createClassBinder() instead. */
    exports.Icon = icon;
    /** @deprecated Use createClassBinder() instead. */
    function deprecated(message) {
        return function (target, name) {
            legacy_decorators_check(name);
            if (typeof name === "undefined") {
                jsb.internal.set_script_doc(target, undefined, 0, message ?? "");
                return;
            }
            if (typeof name !== "string" || !name) {
                throw new Error("Only methods/properties with a string name/key can be marked as deprecated");
            }
            jsb.internal.set_script_doc(target, name, 0, message ?? "");
        };
    }
    /** @deprecated Use createClassBinder() instead. */
    exports.Deprecated = deprecated;
    /** @deprecated Use createClassBinder() instead. */
    function experimental(message) {
        return function (target, name) {
            legacy_decorators_check(name);
            if (typeof name === "undefined") {
                jsb.internal.set_script_doc(target, undefined, 1, message ?? "");
                return;
            }
            if (typeof name !== "string" || !name) {
                throw new Error("Only methods/properties with a string name/key can be marked as experimental");
            }
            jsb.internal.set_script_doc(target, name, 1, message ?? "");
        };
    }
    /** @deprecated Use createClassBinder() instead. */
    exports.Experimental = experimental;
    /** @deprecated Use createClassBinder() instead. */
    function help(message) {
        return function (target, name) {
            legacy_decorators_check(name);
            if (typeof name === "undefined") {
                jsb.internal.set_script_doc(target, undefined, 2, message ?? "");
                return;
            }
            if (typeof name !== "string" || !name) {
                throw new Error("Only methods/properties with a string name/key can be given a help string");
            }
            jsb.internal.set_script_doc(target, name, 2, message ?? "");
        };
    }
    /** @deprecated Use createClassBinder() instead. */
    exports.Help = help;
    function createClassBinder() {
        const signal_names = [];
        const property_info_map = {};
        const rpc_map = {};
        const onready_map = {};
        let is_tool = false;
        let icon_path = undefined;
        const deprecated_map = {};
        const experimental_map = {};
        const help_map = {};
        let executed = false;
        const hint_string_name = jsb.internal.names.get_member("hint_string");
        // primary class decorator
        function bind_class() {
            if (executed) {
                throw new Error("Re-using the same class binder across multiple classes is not supported");
            }
            executed = true;
            return (target, context) => {
                if (typeof context !== "object") {
                    throw new Error("The createClassBinder() requires modern decorator support. Disable legacy decorators (experimentalDecorators) in your tsconfig.json");
                }
                if (!context.name) {
                    throw new Error("Only named classes can be exported for consumption by Godot");
                }
                const proto = target.prototype;
                for (const signal_name of signal_names) {
                    jsb.internal.add_script_signal(proto, signal_name);
                }
                for (const info of Object.values(property_info_map)) {
                    jsb.internal.add_script_property(proto, info);
                }
                for (const [name, config] of Object.entries(rpc_map)) {
                    jsb.internal.add_script_rpc(proto, name, {
                        rpc_mode: config.mode,
                        call_local: typeof config.sync !== "undefined" ? config.sync == "call_local" : undefined,
                        transfer_mode: config.transfer_mode,
                        channel: config.transfer_channel,
                    });
                }
                for (const [name, evaluator] of Object.entries(onready_map)) {
                    jsb.internal.add_script_ready(proto, { name, evaluator });
                }
                if (is_tool) {
                    jsb.internal.add_script_tool(target);
                }
                if (icon_path) {
                    jsb.internal.add_script_icon(target, icon_path);
                }
                for (const [name, message] of Object.entries(deprecated_map)) {
                    jsb.internal.set_script_doc(proto, name, 0, message ?? "");
                }
                for (const [name, message] of Object.entries(experimental_map)) {
                    jsb.internal.set_script_doc(proto, name, 1, message ?? "");
                }
                for (const [name, message] of Object.entries(help_map)) {
                    jsb.internal.set_script_doc(proto, name, 2, message ?? "");
                }
            };
        }
        function add_property(name, type, options) {
            if (property_info_map[name]) {
                throw new Error(`Property ${name} already exported. You must decorate a getter or a setter, not both.`);
            }
            const ebd = {
                name,
                type: type,
                hint: PropertyHint.PROPERTY_HINT_NONE,
                hint_string: "",
                usage: PropertyUsageFlags.PROPERTY_USAGE_DEFAULT,
            };
            if (typeof options === "object") {
                if (typeof options.hint === "number") {
                    ebd.hint = options.hint;
                }
                if (typeof options.usage === "number") {
                    ebd.usage = options.usage;
                }
                if (typeof options.hint_string === "string") {
                    ebd.hint_string = options.hint_string;
                }
                // overwrite hint if class_ is provided
                try {
                    //TODO more general and unified way to handle all types
                    if (type === VariantTypeObject) {
                        const clazz = options.class;
                        if (typeof clazz === "function") {
                            const prototype = clazz.prototype;
                            if (prototype instanceof Resource) {
                                ebd.hint = PropertyHint.PROPERTY_HINT_RESOURCE_TYPE;
                                ebd.hint_string = clazz.name;
                                ebd.usage |= PropertyUsageFlags.PROPERTY_USAGE_SCRIPT_VARIABLE;
                            }
                            else if (prototype instanceof Node ||
                                (clazz[ProxyTarget] ?? clazz) === (Node[ProxyTarget] ?? Node)) {
                                ebd.hint = PropertyHint.PROPERTY_HINT_NODE_TYPE;
                                ebd.hint_string = clazz.name;
                                ebd.usage |= PropertyUsageFlags.PROPERTY_USAGE_SCRIPT_VARIABLE;
                            }
                        }
                        property_info_map[name] = ebd;
                        return;
                    }
                    let hint_string = get_hint_string(options.class);
                    if (hint_string.length > 0) {
                        ebd.hint =
                            type === Variant.Type.TYPE_ARRAY
                                ? PropertyHint.PROPERTY_HINT_ARRAY_TYPE
                                : PropertyHint.PROPERTY_HINT_TYPE_STRING;
                        ebd.hint_string = hint_string;
                        ebd.usage |= PropertyUsageFlags.PROPERTY_USAGE_SCRIPT_VARIABLE;
                    }
                }
                catch (e) {
                    if (ebd.hint === PropertyHint.PROPERTY_HINT_NONE) {
                        console.warn("the given parameters are not supported or not implemented (you need to give hint/hint_string/usage manually)", `prop:${name} type:${type} class_:${guess_type_name(options.class)}`);
                    }
                }
            }
            property_info_map[name] = ebd;
        }
        function bind_export(type, options) {
            return (_target, context) => {
                if (typeof context !== "object") {
                    throw new Error("The createClassBinder() requires modern decorator support. Disable legacy decorators (experimentalDecorators) in your tsconfig.json");
                }
                const name = context.name;
                if (typeof name !== "string") {
                    throw new Error("Only properties with a string name/key can be exported");
                }
                switch (context.kind) {
                    case "accessor":
                    case "field":
                    case "getter":
                    case "setter":
                        add_property(name, type, options && proxy.object_proxy(options, true));
                        return;
                    default: {
                        const _context = context; // Exhaustive check
                        throw new Error(`The export decorator can not be used to decorate a ${context.kind}. Decorate an auto-accessor, setter or field.`);
                    }
                }
            };
        }
        function bind_range(type, min, max, step = 1, ...extra_hints) {
            return bind_export(type, {
                hint: PropertyHint.PROPERTY_HINT_RANGE,
                [hint_string_name]: [min, max, step, ...extra_hints].join(","),
            });
        }
        return proxy.key_only_proxy(Object.assign(bind_class, {
            // additional class decorators
            tool() {
                return function (target, _context) {
                    jsb.internal.add_script_tool(target);
                };
            },
            icon(path) {
                return function (target, _context) {
                    jsb.internal.add_script_icon(target, path);
                };
            },
            // member decorators
            export: Object.assign(bind_export, {
                multiline() {
                    return bind_export(Variant.Type.TYPE_STRING, { hint: PropertyHint.PROPERTY_HINT_MULTILINE_TEXT });
                },
                range(min, max, step, ...extra_hints) {
                    return bind_range(Variant.Type.TYPE_FLOAT, min, max, step, ...extra_hints);
                },
                /** String as a path to a file, custom filter provided as hint. */
                range_int(min, max, step, ...extra_hints) {
                    return bind_range(Variant.Type.TYPE_INT, min, max, step, ...extra_hints);
                },
                file(filter) {
                    return bind_export(Variant.Type.TYPE_STRING, {
                        hint: PropertyHint.PROPERTY_HINT_FILE,
                        [hint_string_name]: filter,
                    });
                },
                dir(filter) {
                    return bind_export(Variant.Type.TYPE_STRING, {
                        hint: PropertyHint.PROPERTY_HINT_DIR,
                        [hint_string_name]: filter,
                    });
                },
                global_file(filter) {
                    return bind_export(Variant.Type.TYPE_STRING, {
                        hint: PropertyHint.PROPERTY_HINT_GLOBAL_FILE,
                        [hint_string_name]: filter,
                    });
                },
                global_dir(filter) {
                    return bind_export(Variant.Type.TYPE_STRING, {
                        hint: PropertyHint.PROPERTY_HINT_GLOBAL_DIR,
                        [hint_string_name]: filter,
                    });
                },
                exp_easing(hint) {
                    return bind_export(Variant.Type.TYPE_FLOAT, {
                        hint: PropertyHint.PROPERTY_HINT_EXP_EASING,
                        [hint_string_name]: hint,
                    });
                },
                /**
                 * A Shortcut for `export_(Variant.Type.TYPE_ARRAY, { class: clazz })`
                 */
                array(clazz) {
                    return bind_export(Variant.Type.TYPE_ARRAY, { class: clazz });
                },
                /**
                 * A Shortcut for exporting a dictionary { class: [key_class, value_class] })`
                 */
                dictionary(key_class, value_class) {
                    return bind_export(Variant.Type.TYPE_DICTIONARY, { class: TypePair(key_class, value_class) });
                },
                object(clazz) {
                    return bind_export(VariantTypeObject, { class: clazz });
                },
                enum(enum_type) {
                    return bind_export(Variant.Type.TYPE_INT, {
                        hint: PropertyHint.PROPERTY_HINT_ENUM,
                        [hint_string_name]: get_hint_string_for_enum(enum_type),
                        usage: PropertyUsageFlags.PROPERTY_USAGE_DEFAULT,
                    });
                },
                flags(enum_type) {
                    const hints = [];
                    for (const [key, value] of Object.entries(enum_type)) {
                        if (typeof value === "number" && value > 0 && Number.isInteger(value)) {
                            hints.push(key + ":" + value);
                        }
                    }
                    return bind_export(Variant.Type.TYPE_INT, {
                        hint: PropertyHint.PROPERTY_HINT_FLAGS,
                        [hint_string_name]: hints.join(","),
                        usage: PropertyUsageFlags.PROPERTY_USAGE_DEFAULT,
                    });
                },
                cache() {
                    return (target, context) => {
                        if (typeof context !== "object") {
                            throw new Error("The createClassBinder() requires modern decorator support. Disable legacy decorators (experimentalDecorators) in your tsconfig.json");
                        }
                        const name = context.name;
                        if (typeof name !== "string") {
                            throw new Error("Only properties with a string name/key can be cached");
                        }
                        const info = property_info_map[name];
                        if (!info) {
                            if (context.kind === "accessor") {
                                throw new Error(`Cache decorator must appear before the export decorator on accessor "${name}"`);
                            }
                            else {
                                throw new Error(`Cache decorated setter must appear after the export decorated getter for property "${name}".`);
                            }
                        }
                        info.cache = true;
                        const update_cached_value = proxy.proxy_unwrap_value(jsb.internal.create_script_cached_property_updater(name));
                        switch (context.kind) {
                            case "accessor": {
                                const set_value = target.set;
                                return {
                                    set: function (value) {
                                        set_value.call(this, value);
                                        invoke_with_this(update_cached_value, this, value);
                                    },
                                };
                            }
                            case "setter": {
                                return function (value) {
                                    target.call(this, value);
                                    invoke_with_this(update_cached_value, this, value);
                                };
                            }
                            default:
                                throw new Error(`The cache decorator can not be used to decorate a ${context.kind}. Decorate an auto-accessor, setter or field.`);
                        }
                    };
                },
            }),
            signal() {
                return (_target, context) => {
                    if (typeof context !== "object") {
                        throw new Error("The createClassBinder() requires modern decorator support. Disable legacy decorators (experimentalDecorators) in your tsconfig.json");
                    }
                    context = proxy.proxy_unwrap_value(context);
                    const name = context.name;
                    if (typeof name !== "string") {
                        throw new Error("Only signals with a string name can be exported");
                    }
                    signal_names.push(name);
                    if (context.kind === "accessor") {
                        return {
                            get: proxy.proxy_unwrap_value(jsb.internal.create_script_signal_getter(name)),
                            set: () => {
                                throw new Error(`Signal properties cannot be reassigned. Did you mean to .connect() a callback instead?`);
                            },
                        };
                    }
                    else if (context.kind === "field") {
                        context.addInitializer(function () {
                            context.access.set(this, invoke_with_this(proxy.proxy_unwrap_value(jsb.internal.create_script_signal_getter(name)), this));
                        });
                        return undefined;
                    }
                    else if (context.kind === "getter") {
                        return proxy.proxy_unwrap_value(jsb.internal.create_script_signal_getter(name));
                    }
                    else {
                        throw new Error(`The signal decorator can not be used to decorate a ${context.kind}. A \`readonly\` field is recommended.`);
                    }
                };
            },
            rpc(config) {
                return (_target, context) => {
                    if (typeof context !== "object") {
                        throw new Error("The createClassBinder() requires modern decorator support. Disable legacy decorators (experimentalDecorators) in your tsconfig.json");
                    }
                    const name = context.name;
                    if (typeof name !== "string") {
                        throw new Error("Only methods with a string name can be remote procedures");
                    }
                    rpc_map[name] = config ? proxy.object_proxy(config, true) : {};
                };
            },
            /**
             * auto initialized on ready (before _ready called)
             * @param evaluator for now, only string is accepted
             */
            onready(evaluator) {
                return (_target, context) => {
                    if (typeof context !== "object") {
                        throw new Error("The createClassBinder() requires modern decorator support. Disable legacy decorators (experimentalDecorators) in your tsconfig.json");
                    }
                    const name = context.name;
                    if (typeof name !== "string") {
                        throw new Error("Only methods with a string name can be registered as an onready callback");
                    }
                    onready_map[name] = evaluator;
                };
            },
            // class or member decorators
            deprecated(message) {
                return function (target, context) {
                    if (typeof context !== "object") {
                        throw new Error("The createClassBinder() requires modern decorator support. Disable legacy decorators (experimentalDecorators) in your tsconfig.json");
                    }
                    if (context.kind === "class") {
                        jsb.internal.set_script_doc(target, undefined, 0, message ?? "");
                        return;
                    }
                    const name = typeof context === "object" ? context.name : context;
                    if (typeof name !== "string") {
                        throw new Error("Only methods/properties with a string name/key can be marked as deprecated");
                    }
                    deprecated_map[name] = message ?? "";
                };
            },
            experimental(message) {
                return function (target, context) {
                    if (typeof context !== "object") {
                        throw new Error("The createClassBinder() requires modern decorator support. Disable legacy decorators (experimentalDecorators) in your tsconfig.json");
                    }
                    if (context.kind === "class") {
                        jsb.internal.set_script_doc(target, undefined, 1, message ?? "");
                        return;
                    }
                    const name = typeof context === "object" ? context.name : context;
                    if (typeof name !== "string") {
                        throw new Error("Only methods/properties with a string name/key can be marked as experimental");
                    }
                    experimental_map[name] = message ?? "";
                };
            },
            help(message) {
                return function (target, context) {
                    if (typeof context !== "object") {
                        throw new Error("The createClassBinder() requires modern decorator support. Disable legacy decorators (experimentalDecorators) in your tsconfig.json");
                    }
                    if (context.kind === "class") {
                        jsb.internal.set_script_doc(target, undefined, 2, message ?? "");
                        return;
                    }
                    const name = typeof context === "object" ? context.name : context;
                    if (typeof name !== "string") {
                        throw new Error("Only methods/properties with a string name/key can be marked as help");
                    }
                    help_map[name] = message ?? "";
                };
            },
        }));
    }
});
define("godot.typeloader", ["require", "exports"], function (require, exports) {
    "use strict";
    Object.defineProperty(exports, "__esModule", { value: true });
    exports.on_type_loaded = on_type_loaded;
    const type_db = {};
    class TypeProcessor {
        // avoid cyclic call on the same type
        locked = false;
        callbacks = [];
        push(callback) {
            if (this.locked) {
                throw new Error("TypeProcessor is locked");
            }
            this.callbacks.push(callback);
            return this;
        }
        exec(type) {
            if (this.locked) {
                throw new Error("TypeProcessor is locked");
            }
            this.locked = true;
            for (let cb of this.callbacks) {
                try {
                    cb(type);
                }
                catch (e) {
                    console.error(e);
                }
            }
            this.locked = false;
        }
    }
    const type_processors = new Map();
    function _on_type_loaded(type_name, callback) {
        if (typeof type_name !== "string") {
            throw new Error("type_name must be a string");
        }
        if (typeof type_db[type_name] !== "undefined") {
            callback(type_db[type_name]);
            return;
        }
        if (type_processors.has(type_name)) {
            type_processors.get(type_name).push(callback);
        }
        else {
            type_processors.set(type_name, new TypeProcessor().push(callback));
        }
    }
    // callback on a godot type loaded by jsb_godot_module_loader.
    // each callback will be called only once.
    function on_type_loaded(type_name, callback) {
        if (typeof type_name === "string") {
            _on_type_loaded(type_name, callback);
        }
        else if (Array.isArray(type_name)) {
            for (let name of type_name) {
                _on_type_loaded(name, callback);
            }
        }
        else {
            throw new Error("type_name must be a string or an array of strings");
        }
    }
    // callback on a godot type loaded by jsb_godot_module_loader
    exports._mod_proxy_ = function (builtin_symbols, type_loader_func) {
        return new Proxy(type_db, {
            set: function (target, prop_name, value) {
                if (typeof prop_name !== "string") {
                    throw new Error(`only string key is allowed`);
                }
                if (typeof target[prop_name] !== "undefined") {
                    console.warn("overwriting existing value", prop_name);
                }
                target[prop_name] = value;
                return true;
            },
            get: function (target, prop_name) {
                let o = target[prop_name];
                if (typeof o === "undefined" && typeof prop_name === "string") {
                    o = target[prop_name] =
                        typeof builtin_symbols[prop_name] !== "undefined"
                            ? builtin_symbols[prop_name]
                            : type_loader_func(prop_name);
                }
                return o;
            },
        });
    };
    exports._post_bind_ = function (type_name, type) {
        const processors = type_processors.get(type_name);
        if (processors !== undefined) {
            processors.exec(type);
            type_processors.delete(type_name);
        }
    };
});
define("jsb.core", ["require", "exports"], function (require, exports) {
    "use strict";
    Object.defineProperty(exports, "__esModule", { value: true });
    const { jsb } = require("godot.lib.api");
    // [WARNING] ALL IMPLEMENTATIONS BELOW ARE FOR BACKWARD COMPATIBILITY ONLY.
    // [WARNING] THEY EXIST TO TEMPORARILY SUPPORT OLD CODES THAT USE THESE FUNCTIONS.
    // [WARNING] FOLLOW THE CHANGES IN `https://github.com/godotjs/GodotJS/tree/main/docs/breaking_changes.md` TO UPDATE YOUR CODES.
    /**
     * FOR BACKWARD COMPATIBILITY ONLY
     * @deprecated [WARNING] This function is deprecated. Use `SignalN<..., R>.as_promise()` instead.
     */
    exports.$wait = function (signal) {
        return new Promise((resolve) => {
            let fn = null;
            fn = require("godot.lib.api").create(function () {
                signal.disconnect(fn);
                if (arguments.length == 0) {
                    resolve(undefined);
                    return;
                }
                if (arguments.length == 1) {
                    resolve(arguments[0]);
                    return;
                }
                // return as javascript array if more than one
                resolve(Array.from(arguments));
                jsb.internal.notify_microtasks_run();
            });
            signal.connect(fn, 0);
        });
    };
    /**
     * Wait for seconds as a promise.
     * ```typescript
     * function seconds(secs: number) {
     *    return new Promise(function (resolve) {
     *        setTimeout(function () {
     *            resolve(undefined);
     *        }, secs * 1000);
     *    });
     *}
     * ```
     * FOR BACKWARD COMPATIBILITY ONLY
     * @deprecated [WARNING] This function is deprecated. Implement your own version of this function.
     * @param secs time to wait in seconds
     * @returns Promise to await
     */
    exports.seconds = function (secs) {
        return new Promise(function (resolve) {
            setTimeout(function () {
                resolve(undefined);
            }, secs * 1000);
        });
    };
    /**
     * FOR BACKWARD COMPATIBILITY ONLY
     * @deprecated [WARNING] This function is deprecated. Use the same function from `godot.annotations` instead.
     */
    exports.signal = function () {
        return function (target, key) {
            jsb.internal.add_script_signal(target, key);
        };
    };
    /**
     * FOR BACKWARD COMPATIBILITY ONLY
     * @deprecated [WARNING] This function is deprecated. Use the same function from `godot.annotations` instead.
     */
    exports.export_multiline = function () {
        const { PropertyHint, Variant } = require("godot.lib.api");
        return exports.export_(Variant.Type.TYPE_STRING, { hint: PropertyHint.PROPERTY_HINT_MULTILINE_TEXT });
    };
    function __export_range(type, min, max, step = 1, ...extra_hints) {
        const { PropertyHint } = require("godot.lib.api");
        let hint_string = `${min},${max},${step}`;
        if (typeof extra_hints !== "undefined") {
            hint_string += "," + extra_hints.join(",");
        }
        return exports.export_(type, { hint: PropertyHint.PROPERTY_HINT_RANGE, hint_string: hint_string });
    }
    /**
     * FOR BACKWARD COMPATIBILITY ONLY
     * @deprecated [WARNING] This function is deprecated. Use the same function from `godot.annotations` instead.
     */
    exports.export_range = function (min, max, step = 1, ...extra_hints) {
        const { Variant } = require("godot.lib.api");
        return __export_range(Variant.Type.TYPE_FLOAT, min, max, step, ...extra_hints);
    };
    /**
     * FOR BACKWARD COMPATIBILITY ONLY
     * @deprecated [WARNING] This function is deprecated. Use the same function from `godot.annotations` instead.
     */
    exports.export_range_i = function (min, max, step = 1, ...extra_hints) {
        const { Variant } = require("godot.lib.api");
        return __export_range(Variant.Type.TYPE_INT, min, max, step, ...extra_hints);
    };
    /**
     * FOR BACKWARD COMPATIBILITY ONLY
     * @deprecated [WARNING] This function is deprecated. Use the same function from `godot.annotations` instead.
     */
    exports.export_file = function (filter) {
        const { PropertyHint, Variant } = require("godot.lib.api");
        return exports.export_(Variant.Type.TYPE_STRING, { hint: PropertyHint.PROPERTY_HINT_FILE, hint_string: filter });
    };
    /**
     * FOR BACKWARD COMPATIBILITY ONLY
     * @deprecated [WARNING] This function is deprecated. Use the same function from `godot.annotations` instead.
     */
    exports.export_dir = function (filter) {
        const { PropertyHint, Variant } = require("godot.lib.api");
        return exports.export_(Variant.Type.TYPE_STRING, { hint: PropertyHint.PROPERTY_HINT_DIR, hint_string: filter });
    };
    /**
     * FOR BACKWARD COMPATIBILITY ONLY
     * @deprecated [WARNING] This function is deprecated. Use the same function from `godot.annotations` instead.
     */
    exports.export_global_file = function (filter) {
        const { PropertyHint, Variant } = require("godot.lib.api");
        return exports.export_(Variant.Type.TYPE_STRING, {
            hint: PropertyHint.PROPERTY_HINT_GLOBAL_FILE,
            hint_string: filter,
        });
    };
    /**
     * FOR BACKWARD COMPATIBILITY ONLY
     * @deprecated [WARNING] This function is deprecated. Use the same function from `godot.annotations` instead.
     */
    exports.export_global_dir = function (filter) {
        const { PropertyHint, Variant } = require("godot.lib.api");
        return exports.export_(Variant.Type.TYPE_STRING, {
            hint: PropertyHint.PROPERTY_HINT_GLOBAL_DIR,
            hint_string: filter,
        });
    };
    /**
     * FOR BACKWARD COMPATIBILITY ONLY
     * @deprecated [WARNING] This function is deprecated. Use the same function from `godot.annotations` instead.
     */
    exports.export_exp_easing = function (hint) {
        const { PropertyHint, Variant } = require("godot.lib.api");
        return exports.export_(Variant.Type.TYPE_FLOAT, { hint: PropertyHint.PROPERTY_HINT_EXP_EASING, hint_string: hint });
    };
    /**
     * FOR BACKWARD COMPATIBILITY ONLY
     * @deprecated [WARNING] This function is deprecated. Use the same function from `godot.annotations` instead.
     */
    exports.export_ = function (type, details) {
        const { PropertyHint, PropertyUsageFlags } = require("godot.lib.api");
        return function (target, key) {
            let ebd = {
                name: key,
                type: type,
                hint: PropertyHint.PROPERTY_HINT_NONE,
                hint_string: "",
                usage: PropertyUsageFlags.PROPERTY_USAGE_DEFAULT,
            };
            if (typeof details === "object") {
                if (typeof details.hint === "number")
                    ebd.hint = details.hint;
                if (typeof details.hint_string === "string")
                    ebd.hint_string = details.hint_string;
                if (typeof details.usage === "number")
                    ebd.usage = details.usage;
            }
            jsb.internal.add_script_property(target, ebd);
        };
    };
    /**
     * FOR BACKWARD COMPATIBILITY ONLY
     * @deprecated [WARNING] This function is deprecated. Use the same function from `godot.annotations` instead.
     */
    exports.export_enum = function (enum_type) {
        const { PropertyHint, PropertyUsageFlags, Variant } = require("godot.lib.api");
        return function (target, key) {
            let enum_vs = [];
            for (let c in enum_type) {
                const v = enum_type[c];
                if (typeof v === "string") {
                    enum_vs.push(v + ":" + c);
                }
            }
            let ebd = {
                name: key,
                type: Variant.Type.TYPE_INT,
                hint: PropertyHint.PROPERTY_HINT_ENUM,
                hint_string: enum_vs.join(","),
                usage: PropertyUsageFlags.PROPERTY_USAGE_DEFAULT,
            };
            jsb.internal.add_script_property(target, ebd);
        };
    };
    /**
     * FOR BACKWARD COMPATIBILITY ONLY
     * @deprecated [WARNING] This function is deprecated. Use the same function from `godot.annotations` instead.
     */
    exports.export_flags = function (enum_type) {
        const { PropertyHint, PropertyUsageFlags, Variant } = require("godot.lib.api");
        return function (target, key) {
            let enum_vs = [];
            for (let c in enum_type) {
                const v = enum_type[c];
                if (typeof v === "string" && enum_type[v] != 0) {
                    enum_vs.push(v + ":" + c);
                }
            }
            let ebd = {
                name: key,
                type: Variant.Type.TYPE_INT,
                hint: PropertyHint.PROPERTY_HINT_FLAGS,
                hint_string: enum_vs.join(","),
                usage: PropertyUsageFlags.PROPERTY_USAGE_DEFAULT,
            };
            jsb.internal.add_script_property(target, ebd);
        };
    };
    /**
     * FOR BACKWARD COMPATIBILITY ONLY
     * @deprecated [WARNING] This function is deprecated. Use the same function from `godot.annotations` instead.
     */
    exports.rpc = function (config) {
        return function (target, propertyKey) {
            if (typeof propertyKey !== "string") {
                throw new Error("only string is allowed as propertyKey for rpc config");
                return;
            }
            if (typeof config !== "undefined") {
                jsb.internal.add_script_rpc(target, propertyKey, {
                    mode: config.mode,
                    sync: typeof config.sync !== "undefined" ? config.sync == "call_local" : undefined,
                    transfer_mode: config.transfer_mode,
                    transfer_channel: config.transfer_channel,
                });
            }
            else {
                jsb.internal.add_script_rpc(target, propertyKey, {});
            }
        };
    };
    /**
     * FOR BACKWARD COMPATIBILITY ONLY
     * @deprecated [WARNING] This function is deprecated. Use the same function from `godot.annotations` instead.
     */
    exports.onready = function (evaluator) {
        return function (target, key) {
            let ebd = { name: key, evaluator: evaluator };
            jsb.internal.add_script_ready(target, ebd);
        };
    };
    /**
     * FOR BACKWARD COMPATIBILITY ONLY
     * @deprecated [WARNING] This function is deprecated. Use the same function from `godot.annotations` instead.
     */
    exports.tool = function () {
        return function (target) {
            jsb.internal.add_script_tool(target);
        };
    };
    /**
     * FOR BACKWARD COMPATIBILITY ONLY
     * @deprecated [WARNING] This function is deprecated. Use the same function from `godot.annotations` instead.
     */
    exports.icon = function (path) {
        return function (target) {
            jsb.internal.add_script_icon(target, path);
        };
    };
    /**
     * FOR BACKWARD COMPATIBILITY ONLY
     * @deprecated [WARNING] This function is deprecated. Use the same function from `godot.annotations` instead.
     */
    exports.deprecated = function (message) {
        return function (target, propertyKey) {
            if (typeof propertyKey === "undefined") {
                jsb.internal.set_script_doc(target, undefined, 0, message ?? "");
                return;
            }
            if (typeof propertyKey !== "string" || propertyKey.length == 0)
                throw new Error("only string key is allowed for doc");
            jsb.internal.set_script_doc(target, propertyKey, 0, message ?? "");
        };
    };
    /**
     * FOR BACKWARD COMPATIBILITY ONLY
     * @deprecated [WARNING] This function is deprecated. Use the same function from `godot.annotations` instead.
     */
    exports.experimental = function (message) {
        return function (target, propertyKey) {
            if (typeof propertyKey === "undefined") {
                jsb.internal.set_script_doc(target, undefined, 1, message ?? "");
                return;
            }
            if (typeof propertyKey !== "string" || propertyKey.length == 0)
                throw new Error("only string key is allowed for doc");
            jsb.internal.set_script_doc(target, propertyKey, 1, message ?? "");
        };
    };
    /**
     * FOR BACKWARD COMPATIBILITY ONLY
     * @deprecated [WARNING] This function is deprecated. Use the same function from `godot.annotations` instead.
     */
    exports.help = function (message) {
        return function (target, propertyKey) {
            if (typeof propertyKey === "undefined") {
                jsb.internal.set_script_doc(target, undefined, 2, message ?? "");
                return;
            }
            if (typeof propertyKey !== "string" || propertyKey.length == 0)
                throw new Error("only string key is allowed for doc");
            jsb.internal.set_script_doc(target, propertyKey, 2, message ?? "");
        };
    };
    /**
     * FOR BACKWARD COMPATIBILITY ONLY
     * @deprecated [WARNING] This function is deprecated. Use the same function from `godot` instead.
     */
    exports.GLOBAL_GET = function (entry_path) {
        const { ProjectSettings } = require("godot.lib.api");
        return ProjectSettings.get_setting_with_override(entry_path);
    };
    /**
     * FOR BACKWARD COMPATIBILITY ONLY
     * @deprecated [WARNING] This function is deprecated. Use the same function from `godot` instead.
     */
    exports.EDITOR_GET = function (entry_path) {
        const { EditorInterface } = require("godot.lib.api");
        return EditorInterface.get_editor_settings().get(entry_path);
    };
});
define("jsb.inject", ["require", "exports"], function (require, exports) {
    "use strict";
    Object.defineProperty(exports, "__esModule", { value: true });
    const proxyable_prototypes = [];
    let helpers = null;
    function get_helpers() {
        if (!helpers) {
            const { GArray, GDictionary, ProxyTarget } = require("godot");
            const get_member = require("godot-jsb").internal.names.get_member;
            helpers = {
                get_member,
                proxy_wrap: function (value) {
                    if (typeof value !== "object" || value === null) {
                        return value;
                    }
                    const proto = Object.getPrototypeOf(value);
                    return proto && proxyable_prototypes.includes(proto) ? value.proxy() : value;
                },
                proxy_unwrap: function (value) {
                    if (typeof value !== "object" || value === null) {
                        return value;
                    }
                    return value[ProxyTarget] ?? value;
                },
                godot_wrap: function (value) {
                    if (typeof value !== "object" || value === null) {
                        return value;
                    }
                    if (Array.isArray(value)) {
                        return GArray.create(value);
                    }
                    const proto = Object.getPrototypeOf(value);
                    if (proto === Object.prototype || proto === null) {
                        return GDictionary.create(value);
                    }
                    return value;
                },
                ProxyTarget: ProxyTarget,
            };
        }
        return helpers;
    }
    require("godot.typeloader").on_type_loaded("GArray", function (type) {
        const helpers = get_helpers();
        const { get_member, godot_wrap, proxy_unwrap, proxy_wrap } = helpers;
        const ProxyTarget = helpers.ProxyTarget;
        proxyable_prototypes.push(type.prototype);
        type.prototype[Symbol.iterator] = function* () {
            const get_indexed = Reflect.get(this, get_member("get_indexed"));
            for (let i = 0; i < this.size(); ++i) {
                yield Reflect.apply(get_indexed, this, [i]);
            }
        };
        // We're not going to try expose the whole Array API, we'll just be super minimalistic. If the user is after
        // something more complex, it'll likely be more performant to spread the GArray into a JS array anyway.
        const pop_back_name = get_member("pop_back");
        const push_back_name = get_member("push_back");
        const array_api = {
            forEach: function (callback, thisArg) {
                const target = this[ProxyTarget];
                let i = 0;
                for (const value of target) {
                    Reflect.apply(callback, thisArg ?? this, [proxy_wrap(value), i++]);
                }
            },
            includes: function (value) {
                const target = this[ProxyTarget];
                return target.has(proxy_unwrap(value));
            },
            indexOf: function (value, fromIndex) {
                const target = this[ProxyTarget];
                return target.find(proxy_unwrap(value), fromIndex);
            },
            pop: function () {
                const target = this[ProxyTarget];
                const result = Reflect.get(target, pop_back_name)();
                return result == null ? result : proxy_wrap(result);
            },
            push: function (...values) {
                const target = this[ProxyTarget];
                const push = Reflect.get(target, push_back_name);
                for (const value of values) {
                    Reflect.apply(push, target, [proxy_unwrap(value)]);
                }
                return target.size();
            },
            toJSON: function (key = "") {
                return [...this];
            },
            toString: function (index) {
                return [...this].map((v) => v?.toString?.() ?? v).join(",");
            },
        };
        const array_iterator = function* () {
            for (let i = 0; i < this.length; ++i) {
                yield this[i];
            }
        };
        const handler = {
            get(target, p, receiver) {
                if (typeof p !== "string") {
                    return p === ProxyTarget ? target : p === Symbol.iterator ? array_iterator : undefined;
                }
                const num = Number.parseInt(p);
                if (!Number.isFinite(num)) {
                    if (p === "length") {
                        return target.size();
                    }
                    return array_api[p];
                }
                if (num < 0 || num >= target.size()) {
                    return undefined;
                }
                return proxy_wrap(target.get(num));
            },
            getOwnPropertyDescriptor(target, p) {
                if (typeof p !== "string") {
                    return undefined;
                }
                const num = Number.parseInt(p);
                if (!(num >= 0) || num >= target.size()) {
                    return undefined;
                }
                return {
                    configurable: true,
                    enumerable: true,
                    value: proxy_wrap(target.get(num)),
                    writable: true,
                };
            },
            has(target, p) {
                if (typeof p !== "string") {
                    return p === Symbol.iterator;
                }
                const num = Number.parseInt(p);
                if (!(num >= 0)) {
                    return p === "length" || !!array_api[p];
                }
                return num >= 0 && num < target.size();
            },
            isExtensible(target) {
                return true;
            },
            ownKeys(target) {
                const keys = [];
                for (let i = 0; i < target.size(); i++) {
                    keys.push(i.toString());
                }
                return keys;
            },
            preventExtensions(target) {
                return true;
            },
            set(target, p, newValue, receiver) {
                if (typeof p !== "string") {
                    return false;
                }
                const num = Number.parseInt(p);
                if (!(num >= 0) || num >= target.size()) {
                    return false;
                }
                target.set(num, proxy_unwrap(newValue));
                return true;
            },
            setPrototypeOf(target, v) {
                return false;
            },
        };
        type.prototype.proxy = function () {
            return new Proxy(this, handler);
        };
        type.create = function (values) {
            const arr = new type();
            const proxy = arr.proxy();
            Reflect.apply(proxy.push, proxy, values.map(godot_wrap));
            return arr;
        };
    });
    require("godot.typeloader").on_type_loaded("GDictionary", function (type) {
        const helpers = get_helpers();
        const { get_member, godot_wrap, proxy_unwrap, proxy_wrap } = helpers;
        const ProxyTarget = helpers.ProxyTarget;
        proxyable_prototypes.push(type.prototype);
        type.prototype[Symbol.iterator] = function* () {
            const keys = this.keys();
            const arr_get_indexed = keys[get_member("get_indexed")];
            const dict_get_keyed = this[get_member("get_keyed")];
            for (let i = 0; i < keys.size(); ++i) {
                const key = Reflect.apply(arr_get_indexed, keys, [i]);
                yield { key: key, value: Reflect.apply(dict_get_keyed, this, [key]) };
            }
        };
        const handler = {
            defineProperty(target, property, attributes) {
                return false;
            },
            deleteProperty(target, p) {
                target.erase(p);
                return true;
            },
            get(target, p, receiver) {
                if (typeof p !== "string") {
                    return p === ProxyTarget ? target : undefined;
                }
                const value = target.get(p);
                return value !== null
                    ? proxy_wrap(value)
                    : target.has(p)
                        ? value
                        : p === "toString"
                            ? Object.prototype.toString
                            : undefined;
            },
            getOwnPropertyDescriptor(target, p) {
                if (typeof p !== "string") {
                    return undefined;
                }
                return {
                    configurable: true,
                    enumerable: true,
                    value: proxy_wrap(target.get(p)),
                    writable: true,
                };
            },
            has(target, p) {
                if (typeof p !== "string") {
                    return false;
                }
                return target.has(p) || p === "toString";
            },
            isExtensible(target) {
                return true;
            },
            ownKeys(target) {
                const keys = [];
                for (const key of target.keys()) {
                    if (typeof key === "string") {
                        keys.push(key);
                    }
                }
                return keys;
            },
            preventExtensions(target) {
                return false;
            },
            set(target, p, newValue, receiver) {
                if (typeof p !== "string") {
                    return false;
                }
                target.set(p, proxy_unwrap(newValue));
                return true;
            },
            setPrototypeOf(target, v) {
                return false;
            },
        };
        type.prototype.proxy = function () {
            return new Proxy(this, handler);
        };
        type.create = function (entries) {
            const dict = new type();
            const proxy = dict.proxy();
            for (const [key, value] of Object.entries(entries)) {
                proxy[key] = godot_wrap(value);
            }
            return dict;
        };
    });
    require("godot.typeloader").on_type_loaded("Callable", function (type) {
        const original_cc = type.create;
        const custom_cc = require("godot-jsb").callable;
        type.create = function () {
            const argc = arguments.length;
            if (argc == 1) {
                if (typeof arguments[0] !== "function") {
                    throw new Error("not a function");
                }
                return custom_cc(arguments[0]);
            }
            if (argc == 2) {
                if (typeof arguments[1] !== "function") {
                    return original_cc(arguments[0], arguments[1]);
                }
                return custom_cc(arguments[0], arguments[1]);
            }
            throw new Error("invalid arguments");
        };
    });
    require("godot.typeloader").on_type_loaded("Signal", function (type) {
        let { jsb, Callable, Object } = require("godot.lib.api");
        const get_member = jsb.internal.names.get_member;
        const notify_microtasks_run = jsb.internal.notify_microtasks_run;
        type.prototype[get_member("as_promise")] = function () {
            let self = this;
            return new Promise(function (resolve, reject) {
                let fn = Callable.create(function () {
                    //self.disconnect(fn);
                    if (arguments.length == 0) {
                        resolve(undefined);
                        return;
                    }
                    if (arguments.length == 1) {
                        resolve(arguments[0]);
                        return;
                    }
                    // return as javascript array if more than one
                    resolve(Array.from(arguments));
                    notify_microtasks_run();
                });
                self.connect(fn, Object.ConnectFlags.CONNECT_ONE_SHOT);
                self = undefined;
            });
        };
    });
    (function () {
        Object.defineProperty(require("godot"), "GLOBAL_GET", {
            value: function (entry_path) {
                return require("godot.lib.api").ProjectSettings.get_setting_with_override(entry_path);
            },
        });
        Object.defineProperty(require("godot"), "EDITOR_GET", {
            value: function (entry_path) {
                return require("godot.lib.api").EditorInterface.get_editor_settings().get(entry_path);
            },
        });
    })();
});
require("jsb.inject");
//# sourceMappingURL=jsb.runtime.bundle.js.map