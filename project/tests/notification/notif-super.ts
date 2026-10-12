// uid://bteh1mx2e0s4e This line is generated, don't modify or remove it.
import NotifBase from "./notif-base";

// 派生类定义 `_notification` 并**显式**调 `super`：JS 不隐式调用父类，要调就得自己写。
export default class NotifSuper extends NotifBase {
	_notification(what: number): void {
		if (what == 9101) {
			(globalThis as any).__notif_log.push("super");
		}
		super._notification(what);
	}
}
