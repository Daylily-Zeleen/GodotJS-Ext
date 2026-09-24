// Numeric-slot acceptance for builtin constructors and operators, shared by
// both binding legs (static / dynamic).
//
// The engine's `Variant::can_convert_strict` lists BOOL = {INT, FLOAT, NIL},
// INT = {BOOL, FLOAT, NIL}, FLOAT = {BOOL, INT, NIL} (STRING commented out in
// all three). The project has to marshal exactly that surface -- otherwise a JS
// BigInt fails the constructor overload filter ("no suitable constructor") or,
// worse, the filter passes and the marshaller then rejects ("bad argument N").
//
// Assertions go through reportTestFailure: a raw throw inside _ready would not
// reach start.ts and the suite would falsely report COMPLETED.
import { Node, Projection, String as GString, Vector2, Vector2i } from "godot";
import { reportTestFailure } from "../test-status";

// Every assertion this scenario must make. Hardcoded (not derived) so that
// deleting a case shows up as a failure instead of silently shrinking the run.
const EXPECTED_CHECKS = 21;

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

/** Asserts the call throws -- a string must still be rejected for bool. */
function expectThrows(context: string, fn: () => void): void {
	let threw = false;
	try {
		fn();
	} catch {
		threw = true;
	}
	check(context, threw, "expected a throw, none happened");
}

/** Asserts the call completes -- `undefined` at a no-default position. */
function expectPasses(context: string, fn: () => void): void {
	let failure: unknown = undefined;
	try {
		fn();
	} catch (error) {
		failure = error;
	}
	check(context, failure === undefined, `unexpected throw: ${String(failure)}`);
}

export default class TestNumeric extends Node {
	_ready(): void {
		try {
			section("int slot / builtin constructor", () => {
				// AC4.1: the overload filter used to reject a BigInt because
				// `probe_vt` had no IsBigInt branch, so it probed as VARIANT_MAX.
				const fromBigInt = new Vector2i(2n as unknown as number, 3);
				const fromNumber = new Vector2i(2, 3);
				check("Vector2i(2n,3).x", fromBigInt.x === 2 && fromBigInt.x === fromNumber.x, `got ${String(fromBigInt.x)}`);
				check("Vector2i(2n,3).y", fromBigInt.y === 3 && fromBigInt.y === fromNumber.y, `got ${String(fromBigInt.y)}`);
			});

			section("float slot / builtin constructor", () => {
				// AC4.2: FLOAT accepts INT, so a BigInt has to reach the float
				// marshaller and follow Number() semantics.
				const fromBigInt = new Vector2(2n as unknown as number, 3n as unknown as number);
				check("Vector2(2n,3n).x", fromBigInt.x === 2, `got ${String(fromBigInt.x)}`);
				check("Vector2(2n,3n).y", fromBigInt.y === 3, `got ${String(fromBigInt.y)}`);

				// The existing number path must not regress.
				const fromNumber = new Vector2(2.5, 3.5);
				check("Vector2(2.5,3.5).x", fromNumber.x === 2.5, `got ${String(fromNumber.x)}`);
				check("Vector2(2.5,3.5).y", fromNumber.y === 3.5, `got ${String(fromNumber.y)}`);
			});

			section("operator right operand", () => {
				// AC4.3 / AC4.4: the operator thunk used a bare `As<Int32>()` on
				// the right operand -- a pure handle reinterpretation that reads a
				// BigInt as garbage. Assert the concrete components, not "no
				// throw".
				const base = new Vector2(1, 2);
				const viaBigInt = base.OP_MULTIPLY(2n as unknown as number);
				const viaNumber = base.OP_MULTIPLY(2);
				check("OP_MULTIPLY(2n) matches number", viaBigInt.x === viaNumber.x && viaBigInt.y === viaNumber.y, `${String(viaBigInt.x)},${String(viaBigInt.y)}`);
				check("OP_MULTIPLY(2n) values", viaBigInt.x === 2 && viaBigInt.y === 4, `${String(viaBigInt.x)},${String(viaBigInt.y)}`);

				// ...and the float slot still works. (A boolean right operand is
				// NOT asserted here: the engine registers no Vector2 × bool
				// operator, so `probe_vt` mapping `true` to BOOL finds no table
				// entry and the dynamic fallback rejects it -- correct engine
				// behaviour, nothing to do with the conversion surface.)
				const viaFloat = base.OP_MULTIPLY(2.5);
				check("OP_MULTIPLY(2.5) values", viaFloat.x === 2.5 && viaFloat.y === 5, `${String(viaFloat.x)},${String(viaFloat.y)}`);
			});

			section("bool slot / class method", () => {
				// AC4.5: BOOL takes INT / FLOAT / NIL, with STRING commented out.
				// `Object.set_block_signals(enable)` has no default_value in the
				// api json, so an explicit `undefined` cannot be masked by default
				// substitution -- this isolates the conversion.
				const cases: [string, unknown, boolean][] = [
					["true", true, true],
					["false", false, false],
					["1", 1, true],
					["0", 0, false],
					["1n", 1n, true],
					["0n", 0n, false],
					["null", null, false],
					["undefined", undefined, false],
				];
				for (const [label, value, expected] of cases) {
					let threw = false;
					try {
						this.set_block_signals(value as boolean);
					} catch {
						threw = true;
					}
					const got = threw ? undefined : this.is_blocking_signals();
					check(`set_block_signals(${label})`, !threw && got === expected, `threw=${String(threw)} got=${String(got)} want=${String(expected)}`);
				}
				this.set_block_signals(false);

				// A string is still rejected rather than coerced.
				expectThrows("set_block_signals('') rejects", () => this.set_block_signals("" as unknown as boolean));
			});

			section("default value still wins over conversion", () => {
				// AC4.5b: `undefined` at a position WITH a default must still take
				// the default. `String.strip_edges(target, left=true, right=true)`
				// is the discriminating control: if `undefined` were converted
				// instead, left/right would be false and the spaces would survive.
				check("strip_edges('  x', undefined) takes default", GString.strip_edges("  x", undefined) === "x", JSON.stringify(GString.strip_edges("  x", undefined)));
				check("strip_edges('  x', false) strips nothing", GString.strip_edges("  x", false) === "  x", JSON.stringify(GString.strip_edges("  x", false)));
			});

			section("no-default position converts", () => {
				// AC4.5b: at a position WITHOUT a default, `undefined` is NIL and
				// has to convert (to false) instead of throwing.
				expectPasses("create_depth_correction(undefined)", () => Projection.create_depth_correction(undefined as unknown as boolean));
			});
		} finally {
			// No quit() here: start.ts owns scene teardown and the completion
			// sentinel. Quitting from the scene would kill the engine before the
			// sentinel is printed.
			if (checks !== EXPECTED_CHECKS) {
				reportTestFailure("numeric coverage", `${String(checks)} checks ran, expected exactly ${String(EXPECTED_CHECKS)}`);
			}
			console.warn(`NUMERIC-DIAG checks=${String(checks)} expected=${String(EXPECTED_CHECKS)}`);
		}
	}
}
