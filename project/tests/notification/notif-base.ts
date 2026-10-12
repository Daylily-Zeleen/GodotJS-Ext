// uid://dt2h5ynv1ayw0 This line is generated, don't modify or remove it.
import { Node } from "godot";

// 通知链的基类：`_notification` 只在最底层定义，用来验证派生类不定义时基类仍被调用。
export default class NotifBase extends Node {
	_notification(what: number): void {
		if (what == 9101) {
			(globalThis as any).__notif_log.push("base");
		}
	}
}
