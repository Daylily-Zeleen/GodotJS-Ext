import { Node, Projection } from "godot";

// `Projection.create_depth_correction(flip_y: bool)` is a REGISTERED builtin whose
// bool parameter has NO default_value — so `undefined` cannot be masked by default
// substitution and the conversion result is observable.
//
// Class-method counterpart: `AStarGrid2D.set_jumping_enabled(enabled: bool)`, also
// without a default; read it back through `is_jumping_enabled()`.
export default class ProbeBoolPlain extends Node {
	_ready(): void {
		try {
			const cases: [string, unknown][] = [
				["true", true],
				["false", false],
				["undefined", undefined],
				["null", null],
				["1 (number)", 1],
				["0 (number)", 0],
				["1n (bigint)", 1n],
				["0n (bigint)", 0n],
				["'' (string)", ""],
			];

			console.log("[probe] --- builtin: Projection.create_depth_correction(flip_y) [no default] ---");
			for (const [label, v] of cases) {
				try {
					Projection.create_depth_correction(v as boolean);
					console.log(`[probe] create_depth_correction(${label}) OK`);
				} catch (e) {
					console.log(`[probe] create_depth_correction(${label}) THREW`);
				}
			}

			console.log("[probe] --- class: AStarGrid2D.set_jumping_enabled(enabled) [no default] ---");
			const gd = require("godot") as { AStarGrid2D?: unknown };
			if (typeof gd.AStarGrid2D !== "function") {
				console.log("[probe] AStarGrid2D not exposed, skipping class-method probe");
				return;
			}
			const grid = new (gd.AStarGrid2D as new () => { set_jumping_enabled(v: boolean): void; is_jumping_enabled(): boolean })();
			for (const [label, v] of cases) {
				try {
					grid.set_jumping_enabled(v as boolean);
					console.log(`[probe] set_jumping_enabled(${label}) OK -> is_jumping_enabled=${String(grid.is_jumping_enabled())}`);
				} catch (e) {
					console.log(`[probe] set_jumping_enabled(${label}) THREW`);
				}
			}
		} finally {
			console.log("[probe] done");
			this.get_tree().quit();
		}
	}
}
