/*
 *  godot.worker.d.ts
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
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not,
 *  see <https://www.gnu.org/licenses/>.
 */

declare module "godot.worker" {
    import { GAny, GArray } from "godot";

    class JSWorker {
        constructor(path: string);

        // transfer 用于显式指定要传送的 godot 对象列表。
        // 用户必须确保这些 godot 对象是有效的，否则会发生崩溃。
        postMessage(message: any, transfer?: GArray | ReadonlyArray<NonNullable<GAny>>): void;
        terminate(): void;

        onready?: () => void;
        onmessage?: (message: any) => void;

        /**
         * worker 侧的错误（入口脚本加载失败 / `onmessage` 抛错 / 定时器回调抛错）。
         * 跨隔离区错误一律是 `CrossEnvError`（具体异常在 `cause`）。
         */
        onerror?: (error: CrossEnvError) => void;
    }

    // only available in worker scripts
    const JSWorkerParent:
        | {
            onmessage?: (message: any) => void;

            close(): void;

            // transfer 用于显式指定要传送的 godot 对象列表。
            // 用户必须确保这些 godot 对象是有效的，否则会发生崩溃。
            postMessage(message: any, transfer?: GArray | ReadonlyArray<NonNullable<GAny>>): void;
        }
        | undefined;
}
