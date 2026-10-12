// uid://cvmthl2iui5dy This line is generated, don't modify or remove it.
import NotifBase from "./notif-base";

// 派生类**定义** `_notification` 且不调 `super`：GDScript 语义下基类仍应被调一次。
export default class NotifDerived extends NotifBase {
	_notification(what: number): void {
		if (what == 9101) {
			(globalThis as any).__notif_log.push("derived");
		}
	}
}
