// uid://b422rmkqhihw0 This line is generated, don't modify or remove it.
import { GArray } from "godot";
import type { GDictionary, PropertyInfo } from "godot";
import { Node } from "godot";

// `_get` / `_set` / `_get_property_list` 各只应被调一次（JS 原型链查找命中即止）。
// 计数写进 `globalThis`，由 C++ 用例回读。
export default class HooksBase extends Node {
	_get(property: string): any {
		const g = globalThis as any;
		g.__hook_calls_get = (g.__hook_calls_get ?? 0) + 1;
		return null;
	}
	_set(property: string, value: any): boolean {
		const g = globalThis as any;
		g.__hook_calls_set = (g.__hook_calls_set ?? 0) + 1;
		return false;
	}
	_get_property_list(): GArray<GDictionary<PropertyInfo>> {
		const g = globalThis as any;
		g.__hook_calls_gpl = (g.__hook_calls_gpl ?? 0) + 1;
		return new GArray();
	}
}
