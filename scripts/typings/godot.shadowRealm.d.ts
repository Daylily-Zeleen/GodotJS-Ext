/*
 *  godot.shadowRealm.d.ts
 *
 *  This file is part of:
 *                                GodotJS-Ext
 *              https://github.com/Daylily-Zeleen/GodotJS-Ext
 *
 *  Copyright (c) 2026-present 忘忧の (Daylily-Zeleen)
 *                 - Contact: daylily-zeleen@foxmail.com
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
 * 跨 Realm 访问对象的 symbol 键时，如果不是全局或内置的 symbol 将无法正常访问
 * 
 * NOTE: 在纯 web (不使用其他js引擎的 web 构建) 中不支持 `JSShadowRealm` 与 `TransferableJSShadowRealm`.
 */
declare module "godot.shadowRealm" {
    import { GAny, GArray } from "godot";

    type PrimitiveValueOrCallable = number | string | boolean | null | undefined | symbol | Function | object;
    class JSShadowRealm {
        constructor(params?: {
            startupScript?: string,
            allowImportAnyModule?: boolean,
        });

        evaluate(sourceText: string): PrimitiveValueOrCallable;
        addAllowedModuleSearchPath(searchPath: string): void;
        importValue(specifier: string, bindingName: string): Promise<PrimitiveValueOrCallable>;
        importValueSync(specifier: string, bindingName: string): PrimitiveValueOrCallable;
        terminate(): void;
    }

    class TransferableJSShadowRealm extends JSShadowRealm {
        constructor(params?: {
            startupScript?: string,
            allowImportAnyModule?: boolean,
        });

        postMessage(message: any, transfer?: GArray | ReadonlyArray<NonNullable<GAny>>): void;

        onmessage?: (message: any) => void;

        terminate(): void;

        //TODO not implemented yet
        onerror?: (error: any) => void;
    }

    // 仅能在 TransferableShadowRealm 中访问
    const ShadowRealmParent:
        | {
            postMessage(message: any, transfer?: GArray | ReadonlyArray<NonNullable<GAny>>): void;

            onmessage?: (message: any) => void;

            close(): void;
        }
        | undefined;
}
