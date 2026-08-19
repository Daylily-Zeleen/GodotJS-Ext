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
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not,
 *  see <https://www.gnu.org/licenses/>.
 */

declare module "godot.worker" {
    import { GAny, GArray, Object as GObject } from "godot";

    class JSWorker {
        constructor(path: string);

        postMessage(message: any, transfer?: GArray | ReadonlyArray<NonNullable<GAny>>): void;
        terminate(): void;

        onready?: () => void;
        onmessage?: (message: any) => void;

        //TODO not implemented yet
        onerror?: (error: any) => void;

        /**
         * @deprecated Use onmessage to receive messages sent from postMessage() with transfers included.
         * @param obj
         */
        ontransfer?: (obj: GObject) => void;
    }

    // only available in worker scripts
    const JSWorkerParent:
        | {
              onmessage?: (message: any) => void;

              close(): void;

              /**
               * @deprecated Use the transfer parameter of postMessage instead.
               * @param obj
               */
              transfer(obj: GObject): void;

              postMessage(message: any, transfer?: GArray | ReadonlyArray<NonNullable<GAny>>): void;
          }
        | undefined;
}
