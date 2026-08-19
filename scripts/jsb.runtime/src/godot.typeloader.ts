/*
 *  godot.typeloader.ts
 *
 *  This file is part of:
 *                                GodotJS-Ext
 *              https://github.com/Daylily-Zeleen/GodotJS-Ext
 *
 *  Copyright (c) 2026-present 忘忧の (Daylily-Zeleen)
 *                 - Contact: daylily-zeleen@foxmail.com
 *  Copyright (c) Contributors of GodotJS
 *                 - <https://github.com/godotjs/GodotJS>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not,
 *  see <https://www.gnu.org/licenses/>.
 */

/**
 * @param type the loaded type or function in godot module
 */
export type TypeLoadedCallback = (type: any) => void;

const type_db: any = {};

class TypeProcessor {
    // avoid cyclic call on the same type
    private locked = false;
    private callbacks: Array<TypeLoadedCallback> = [];

    push(callback: TypeLoadedCallback) {
        if (this.locked) {
            throw new Error("TypeProcessor is locked");
        }
        this.callbacks.push(callback);
        return this;
    }

    exec(type: any): void {
        if (this.locked) {
            throw new Error("TypeProcessor is locked");
        }
        this.locked = true;
        for (let cb of this.callbacks) {
            try {
                cb(type);
            } catch (e) {
                console.error(e);
            }
        }
        this.locked = false;
    }
}
const type_processors = new Map<string, TypeProcessor>();

function _on_type_loaded(type_name: string, callback: TypeLoadedCallback) {
    if (typeof type_name !== "string") {
        throw new Error("type_name must be a string");
    }

    if (typeof type_db[type_name] !== "undefined") {
        callback(type_db[type_name]);
        return;
    }

    if (type_processors.has(type_name)) {
        type_processors.get(type_name)!.push(callback);
    } else {
        type_processors.set(type_name, new TypeProcessor().push(callback));
    }
}

// callback on a godot type loaded by jsb_godot_module_loader.
// each callback will be called only once.
export function on_type_loaded(type_name: string | string[], callback: TypeLoadedCallback) {
    if (typeof type_name === "string") {
        _on_type_loaded(type_name, callback);
    } else if (Array.isArray(type_name)) {
        for (let name of type_name) {
            _on_type_loaded(name, callback);
        }
    } else {
        throw new Error("type_name must be a string or an array of strings");
    }
}

// callback on a godot type loaded by jsb_godot_module_loader
exports._mod_proxy_ = function (
    builtin_symbols: { [key in string]?: symbol },
    type_loader_func: (type_name: string) => any,
): any {
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
        get: function (target: any, prop_name) {
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

exports._post_bind_ = function (type_name: string, type: any): void {
    const processors = type_processors.get(type_name);
    if (processors !== undefined) {
        processors.exec(type);
        type_processors.delete(type_name);
    }
};
