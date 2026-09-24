import { Node, Projection, Rect2 } from "godot";

// `Rect2.intersects(b, include_borders)` has default `false`; passing true/false
// both returned true for the earlier rects, so it cannot tell "undefined was
// substituted with the default" apart from "undefined converted to true".
// Use rects that touch only at an edge: include_borders=true -> true, false -> false.
export default class ProbeBoolNil2 extends Node {
	_ready(): void {
		try {
			const a = new Rect2(0, 0, 10, 10);
			const touching = new Rect2(10, 0, 10, 10); // shares exactly the x=10 edge

			const cases: [string, unknown][] = [
				["true", true],
				["false", false],
				["undefined", undefined],
				["null", null],
				["1 (number)", 1],
				["0 (number)", 0],
				["1n (bigint)", 1n],
				["0n (bigint)", 0n],
			];
			for (const [label, v] of cases) {
				try {
					const res = a.intersects(touching, v as boolean);
					console.log(`[probe] edge-touching intersects(include_borders=${label}) -> ${String(res)}`);
				} catch (e) {
					console.log(`[probe] edge-touching intersects(include_borders=${label}) THREW`);
				}
			}
			// Also: does a bool param WITHOUT a default behave the same for undefined?
			// Basis.looking_at(target, up, use_model_front=false) has a default too.
			// Projection.create_depth_correction(flip_y) has NO default -> clean probe.
			// Projection.create_depth_correction(flip_y: bool, z_near: float) -- flip_y has NO default,
			// so a substituted default cannot mask the conversion result.
			for (const [label, v] of cases) {
				try {
					Projection.create_depth_correction(v as boolean, 1.0);
					console.log(`[probe] create_depth_correction(flip_y=${label}) OK`);
				} catch (e) {
					console.log(`[probe] create_depth_correction(flip_y=${label}) THREW`);
				}
			}
		} finally {
			console.log("[probe] done");
			this.get_tree().quit();
		}
	}
}
