// uid://bfuwrjyapwj47 This line is generated, don't modify or remove it.
import { GArray } from "godot";
import type { GDictionary, PropertyInfo } from "godot";
import HooksBase from "./hooks-base";

// 派生类把三个 hook 全部再定义一遍：改前每个会被调 2 次（基类那次 + 派生类那次都打到派生类函数上）。
export default class HooksDerived extends HooksBase {
	_get(property: string): any {
		const g = globalThis as any;
		g.__hook_calls_get = (g.__hook_calls_get ?? 0) + 1;
		return null;
	}
	_set(property: string, value: any): any {
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
