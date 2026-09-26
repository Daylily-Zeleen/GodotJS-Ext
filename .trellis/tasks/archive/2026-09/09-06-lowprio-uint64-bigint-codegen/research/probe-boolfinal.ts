import { Node, String as GString } from "godot";

// DISCRIMINATING control for "undefined at a defaulted position".
// `String.strip_edges(target, left=true, right=true)` — the bool defaults are TRUE, so:
//   - default substitution  -> left=true  -> strip_edges("  x", undefined) === "x"
//   - truthiness conversion -> left=false -> strip_edges("  x", undefined) === "  x"
// A default of `false` (e.g. Rect2.intersects' include_borders) CANNOT discriminate,
// because `false` is both the default and the truthiness of undefined.
export default class ProbeBoolFinal extends Node {
	_ready(): void {
		try {
			const s = "  x";
			console.log(`[probe] baseline strip_edges(s)        = ${JSON.stringify(GString.strip_edges(s))}`);
			console.log(`[probe] baseline strip_edges(s, true)  = ${JSON.stringify(GString.strip_edges(s, true))}`);
			console.log(`[probe] baseline strip_edges(s, false) = ${JSON.stringify(GString.strip_edges(s, false))}`);

			for (const [label, v] of [["undefined", undefined], ["null", null], ["0", 0], ["1", 1], ["0n", 0n], ["1n", 1n], ["''", ""]] as [string, unknown][]) {
				try {
					const r = GString.strip_edges(s, v as boolean);
					console.log(`[probe] strip_edges(s, ${label}) OK -> ${JSON.stringify(r)}`);
				} catch (e) {
					console.log(`[probe] strip_edges(s, ${label}) THREW`);
				}
			}
		} finally {
			console.log("[probe] done");
			this.get_tree().quit();
		}
	}
}
