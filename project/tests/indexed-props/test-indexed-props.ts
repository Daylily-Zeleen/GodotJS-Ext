// Indexed-property accessor coverage, both binding legs (static / dynamic).
//
// Indexed properties (`prop.index >= 0` in the api json) resolve to a dedicated
// thunk pair that forwards an extra constant index to the backing method
// (`get_param_max(6)`, `set_offset(Side, float)`, ...). Under JSB_WITH_STATIC_BINDINGS
// they take the generated `indexed_property_*_thunk` path; otherwise the reflect
// path. Both are exercised here, and every assertion reads the value back
// through the *method* API as well, so a silent type/slot change shows up.
//
// This exists because the static setter path used to pass `nullptr` as
// `object_method_bind_call`'s `r_return`. The engine does
// `memnew_placement(r_return, Variant(mb->call(...)))` unconditionally, so every
// indexed-property write crashed the process. A crash is not a catchable
// failure, so the getter-only coverage that existed before could never see it.
//
// Assertions go through reportTestFailure: a raw throw inside _ready would not
// reach start.ts and the suite would falsely report COMPLETED.
import { Control, CPUParticles2D, Node, NodePath, Viewport } from "godot";
import { reportTestFailure } from "../test-status";

// Every assertion this scenario must make. Hardcoded (not derived) so that
// deleting a case shows up as a failure instead of silently shrinking the run.
const EXPECTED_CHECKS = 32;

let checks = 0;

function check(context: string, condition: boolean, detail?: string): void {
	checks += 1;
	if (!condition) {
		reportTestFailure(context, detail);
	}
}

function section(name: string, fn: () => void): void {
	try {
		fn();
	} catch (error) {
		reportTestFailure(name, error);
	}
}

export default class TestIndexedProps extends Node {
	_ready(): void {
		try {
			// --- float members, min/max share one backing method ---------------
			section("float indexed property (get + set)", () => {
				const parts = new CPUParticles2D();
				// Each min/max pair resolves to get/set_param_min|max with its own
				// constant index; assert each lands on its own slot, both directions.
				parts.initial_velocity_min = 11.5;
				parts.initial_velocity_max = 22.25;
				check("initial_velocity_min set", parts.initial_velocity_min === 11.5, String(parts.initial_velocity_min));
				check("initial_velocity_max set", parts.initial_velocity_max === 22.25, String(parts.initial_velocity_max));
				check("min/max do not alias", parts.initial_velocity_min !== parts.initial_velocity_max, `${String(parts.initial_velocity_min)} vs ${String(parts.initial_velocity_max)}`);
				// cross-check against the method API
				check("param_min(0) agrees", parts.initial_velocity_min === parts.get_param_min(0), String(parts.get_param_min(0)));
				check("param_max(0) agrees", parts.initial_velocity_max === parts.get_param_max(0), String(parts.get_param_max(0)));

				// A later index (damping = 6) must not hit the first slot.
				parts.damping_min = -3.75;
				parts.damping_max = 42.5;
				check("damping_min set", parts.damping_min === -3.75, String(parts.damping_min));
				check("damping_max set", parts.damping_max === 42.5, String(parts.damping_max));
				check("index 6 is a distinct slot", parts.damping_max !== parts.initial_velocity_max, `${String(parts.damping_max)} vs ${String(parts.initial_velocity_max)}`);
				check("get_param_max(6) agrees", parts.damping_max === parts.get_param_max(6), String(parts.get_param_max(6)));
			});

			// --- bool members ---------------------------------------------------
			section("bool indexed property (get + set)", () => {
				const parts = new CPUParticles2D();
				parts.particle_flag_align_y = true;
				check("particle_flag_align_y true", parts.particle_flag_align_y === true, String(parts.particle_flag_align_y));
				parts.particle_flag_align_y = false;
				check("particle_flag_align_y false", parts.particle_flag_align_y === false, String(parts.particle_flag_align_y));
				check("particle_flag agrees", parts.particle_flag_align_y === parts.get_particle_flag(0), String(parts.get_particle_flag(0)));
			});

			// --- object members (this is the case that used to crash) -----------
			section("object indexed property (get + set)", () => {
				const parts = new CPUParticles2D();
				check("curve defaults to null", parts.damping_curve === null, String(parts.damping_curve));
				// Clearing an already-null slot still goes through the setter body
				// (that write is what crashed before the fix).
				parts.damping_curve = null;
				check("curve set null", parts.damping_curve === null, String(parts.damping_curve));
				check("curve agrees with method", parts.damping_curve === parts.get_param_curve(6), String(parts.get_param_curve(6)));
			});

			// --- NodePath members ----------------------------------------------
			section("NodePath indexed property (get + set)", () => {
				const ctl = new Control();
				const path = new NodePath("Path/To/Target");
				// `String(nodePath)` is not the path text (NodePath has no
				// `toString`); `get_concatenated_names()` is the readable view.
				check("focus_neighbor_left defaults empty", ctl.focus_neighbor_left.get_concatenated_names() === "", String(ctl.focus_neighbor_left.get_concatenated_names()));
				ctl.focus_neighbor_left = path;
				check("focus_neighbor_left set", ctl.focus_neighbor_left.get_concatenated_names() === "Path/To/Target", String(ctl.focus_neighbor_left.get_concatenated_names()));
				check("focus_neighbor_left agrees", ctl.focus_neighbor_left.get_concatenated_names() === ctl.get_focus_neighbor(0).get_concatenated_names(), String(ctl.get_focus_neighbor(0).get_concatenated_names()));
			});

			// --- float members whose backing method takes an enum first ---------
			// `Control.set_offset(Side, float)` / `get_offset(Side)`: the leading
			// enum argument is what the constant index feeds. This is the shape
			// whose crash backtrace read `MethodBindT<Control,enum Side,float>::call`.
			section("enum-first-arg float indexed property (get + set)", () => {
				const ctl = new Control();
				ctl.offset_left = -7.5;
				ctl.offset_top = 1.25;
				ctl.offset_right = 100;
				ctl.offset_bottom = -0.5;
				check("offset_left set", ctl.offset_left === -7.5, String(ctl.offset_left));
				check("offset_top set", ctl.offset_top === 1.25, String(ctl.offset_top));
				check("offset_right set", ctl.offset_right === 100, String(ctl.offset_right));
				check("offset_bottom set", ctl.offset_bottom === -0.5, String(ctl.offset_bottom));
				check("offset_left agrees", ctl.offset_left === ctl.get_offset(0), String(ctl.get_offset(0)));
				check("offset_bottom agrees", ctl.offset_bottom === ctl.get_offset(3), String(ctl.get_offset(3)));
				// Four distinct constant indexes on ONE backing method: a shared
				// index would make these alias.
				const offsets = [ctl.offset_left, ctl.offset_top, ctl.offset_right, ctl.offset_bottom];
				check("offset indexes are distinct", offsets.every((v, i) => offsets.indexOf(v) === i), offsets.join(","));

				// get-only sibling: anchors resolve through `get_anchor` with no
				// setter side; keep reading it so the getter-only branch stays
				// covered.
				check("anchor_left get-only", typeof ctl.anchor_left === "number", typeof ctl.anchor_left);
				check("anchor_bottom get-only", typeof ctl.anchor_bottom === "number", typeof ctl.anchor_bottom);
				check("anchor agrees with method", ctl.anchor_left === ctl.get_anchor(0), `${String(ctl.anchor_left)} vs ${String(ctl.get_anchor(0))}`);
			});

			// --- enum-typed getter, declared int --------------------------------
			section("int indexed property (get + set)", () => {
				const vp = new Viewport();
				vp.positional_shadow_atlas_quad_0 = 1;
				check("quad_0 set", vp.positional_shadow_atlas_quad_0 === 1, String(vp.positional_shadow_atlas_quad_0));
				vp.positional_shadow_atlas_quad_3 = 0;
				check("quad_3 set", vp.positional_shadow_atlas_quad_3 === 0, String(vp.positional_shadow_atlas_quad_3));
				// The getter returns an engine enum; all four indexes share it.
				check("quad_0 agrees", vp.positional_shadow_atlas_quad_0 === vp.get_positional_shadow_atlas_quadrant_subdiv(0), String(vp.get_positional_shadow_atlas_quadrant_subdiv(0)));
				check("quad_3 agrees", vp.positional_shadow_atlas_quad_3 === vp.get_positional_shadow_atlas_quadrant_subdiv(3), String(vp.get_positional_shadow_atlas_quadrant_subdiv(3)));
			});
		} finally {
			// No quit() here: start.ts owns scene teardown and the completion
			// sentinel.
			if (checks !== EXPECTED_CHECKS) {
				reportTestFailure("indexed-props coverage", `${String(checks)} checks ran, expected exactly ${String(EXPECTED_CHECKS)}`);
			}
			console.warn(`INDEXED-PROPS-DIAG checks=${String(checks)} expected=${String(EXPECTED_CHECKS)}`);
		}
	}
}
