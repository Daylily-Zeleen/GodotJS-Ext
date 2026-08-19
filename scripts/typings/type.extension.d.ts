/*
 *  type.extension.d.ts
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

type int64 = number;

interface String {
	getExtension(): string
	getBasename(): string
	pathJoin(path: string): string
	hasExtension(ext: string): boolean
	isAbsolutePath(): boolean
	isRelativePath(): boolean
	isResourceFile(): boolean
	getBaseDir(): string
	getFile(): string
	simplifyPath(): string
	isNetworkSharePath(): boolean

	hash(): int64
	hash64(): BigInt // uint64

	format(values: any, placeholder?: string /* = '{_}' */): string
}
