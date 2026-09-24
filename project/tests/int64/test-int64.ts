// 64-bit integer coverage, both directions, shared by both binding legs
// (static / dynamic).
//
// The generated typings declare `type uint64 = number /* || bigint */` and
// `type int64 = number` -- the BigInt arm is commented out because the
// declarations predate this fix. Every BigInt crossing that boundary is marked
// explicitly below rather than weakening the assertions with `any`.
import { Node, Resource, StreamPeerBuffer, instance_from_id, is_instance_id_valid } from "godot";
import { reportTestFailure } from "../test-status";

/** 2^53-1: the largest integer a JS Number represents exactly. */
const MAX_SAFE = BigInt(Number.MAX_SAFE_INTEGER);

/**
 * Typings boundary: a 64-bit slot is declared `number` but accepts a BigInt at
 * runtime (the conversion layer is what this task fixes). All call sites change
 * together once the declarations grow their BigInt arm.
 */
function asSlot(v: bigint): number {
	return v as unknown as number;
}

/** Reinterpret any 64-bit result as its unsigned bit pattern, for comparison. */
function asUint64(v: number | bigint): bigint {
	return BigInt.asUintN(64, typeof v === "bigint" ? v : BigInt(v));
}

/** Names the pad-to-16-digits hex form used in every failure detail. */
function hex64(v: number | bigint): string {
	return `0x${asUint64(v).toString(16).padStart(16, "0")}`;
}

/** A 64-bit value fed to a slot, and the exact bits it must come back as. */
interface SlotCase {
	label: string;
	value: bigint;
}

// Both sides of the 2^53 boundary, plus the high-bit values.
const U64_CASES: SlotCase[] = [
	{ label: "2^53-1", value: BigInt("9007199254740991") },
	{ label: "2^53", value: BigInt("9007199254740992") },
	{ label: "2^63", value: BigInt("9223372036854775808") },
	{ label: "2^63+1", value: BigInt("9223372036854775809") },
	{ label: "2^64-1", value: BigInt("18446744073709551615") },
];

// "Negative with a magnitude above 2^53" -- the shape the one-sided threshold
// used to round through `(double)`, losing the low bits.
const I64_CASES: SlotCase[] = [
	{ label: "-(2^53-1)", value: -BigInt("9007199254740991") },
	{ label: "-2^53", value: -BigInt("9007199254740992") },
	{ label: "INT64_MIN+1", value: BigInt("-9223372036854775807") },
	{ label: "INT64_MIN", value: BigInt("-9223372036854775808") },
];

// Every assertion this scenario must make. Hardcoded (not derived from the case
// arrays) so that deleting a case shows up as a failure instead of silently
// shrinking the suite.
const EXPECTED_CHECKS = 26;

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
		// A throw must surface as a failure: letting it escape _ready would
		// bypass reportTestFailure and the suite would report COMPLETED.
		reportTestFailure(name, error);
	}
}

/** Writes through the dynamic channel, then reads back through the static one. */
function readBackU64(spb: StreamPeerBuffer, value: bigint): number | bigint {
	spb.seek(0);
	spb.call("put_u64", asSlot(value));
	spb.seek(0);
	return spb.get_u64();
}

/** Same for the signed slot. */
function readBackI64(spb: StreamPeerBuffer, value: bigint): number | bigint {
	spb.seek(0);
	spb.call("put_64", asSlot(value));
	spb.seek(0);
	return spb.get_64();
}

export default class Int64 extends Node {
	_ready(): void {
		try {
			section("u64 readback", () => {
				const spb = new StreamPeerBuffer();
				for (const { label, value } of U64_CASES) {
					const got = readBackU64(spb, value);
					if (value > MAX_SAFE) {
						// AC1.2: above the safe range it must leave as a BigInt.
						check(`u64 ${label} typeof`, typeof got === "bigint", `got ${typeof got}`);
					}
					// AC1.2: and carry its exact bits, not a rounded value.
					check(`u64 ${label} bits`, asUint64(got) === value, `${hex64(got)} != ${hex64(value)}`);
				}
			});

			section("i64 readback", () => {
				const spb = new StreamPeerBuffer();
				for (const { label, value } of I64_CASES) {
					const got = readBackI64(spb, value);
					// AC1.3: within the safe range it stays a Number; beyond it a
					// BigInt. The signed direction is the one that used to be
					// rounded regardless of magnitude.
					const expected = value >= -MAX_SAFE ? "number" : "bigint";
					check(`i64 ${label} typeof`, typeof got === expected, `got ${typeof got}, want ${expected}`);
					check(`i64 ${label} bits`, asUint64(got) === asUint64(value), `${hex64(got)} != ${hex64(value)}`);
				}
			});

			section("ObjectID round-trip", () => {
				// AC1.1: the ordinary handle round-trip has to work. `this` is a
				// live Node already in the tree, so the case needs no allocation
				// and no explicit free.
				const selfId: number | bigint = this.get_instance_id();
				// AC1.3: a non-RefCounted id is small, so it must stay a Number --
				// the fix must not turn every id into a BigInt.
				check("Node id typeof", typeof selfId === "number", `got ${typeof selfId}`);
				check("Node id valid", is_instance_id_valid(asSlot(BigInt(selfId))));
				check("Node id round-trip", instance_from_id(asSlot(BigInt(selfId))) === this);

				// RefCounted ids set bit 63 (`is_ref_counted`), so this is the
				// case that used to be silently corrupted.
				const res = new Resource();
				const resId: number | bigint = res.get_instance_id();
				// AC1.4: it must leave unsigned, i.e. a positive BigInt.
				check("Resource id typeof", typeof resId === "bigint", `got ${typeof resId}`);
				check("Resource id positive", typeof resId === "bigint" && resId > 0n, `got ${String(resId)}`);
				check("Resource id bit63", asUint64(resId) >> 63n === 1n, `bits ${hex64(resId)}`);
				check("Resource id valid", is_instance_id_valid(asSlot(asUint64(resId))));
				check("Resource id round-trip", instance_from_id(asSlot(asUint64(resId))) === res);

				// Independently confirmed against `to_string()`, which formats the
				// id through `itos` -- a path with no double in it, so it is the
				// lossless reference. `itos` prints the *signed* view while
				// `resId` is now unsigned, so both sides are normalized to their
				// bit pattern before comparing.
				const exactText: string = res.to_string();
				const exactId = exactText.replace(/^.*#/, "").replace(/>$/, "");
				check("Resource id exact bits", asUint64(BigInt(exactId)) === asUint64(resId), `${hex64(BigInt(exactId))} != ${hex64(resId)}`);
			});
		} finally {
			// No quit() here: start.ts owns scene teardown and the completion
			// sentinel. Quitting from the scene would kill the engine before the
			// sentinel is printed, and the suite would look like it never ran.
			if (checks !== EXPECTED_CHECKS) {
				reportTestFailure("int64 coverage", `${String(checks)} checks ran, expected exactly ${String(EXPECTED_CHECKS)}`);
			}
			console.warn(`INT64-DIAG checks=${String(checks)} expected=${String(EXPECTED_CHECKS)}`);
		}
	}
}
