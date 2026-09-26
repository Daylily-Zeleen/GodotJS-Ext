// uid://biyom8jmnnfv3 This line is generated, don't modify or remove it.
// 64-bit integer coverage, both directions.
//
// Two axes this scenario has to cover without being rebuilt per configuration:
//
//   - binding leg (static / shared / dynamic) -- the whole point of task A/B.
//   - `JSB_WITH_BIGINT` (jsb.config.h), exported to JS as `BIGINT_FOR_64BIT`.
//     On (the default): a 64-bit value above 2^53-1 leaves Godot as a `BigInt`,
//     so an ObjectID round trip is lossless, and the typings alias is
//     the generated alias `int64` is `number | bigint`. Off: BigInt does not
//     exist in that build at all, so the value leaves as the lossy `Number` and
//     `int64` is a plain `number`.
//     Either way the JS -> Godot direction must not start throwing, which is
//     what the write section pins.
//
// The generated typings declare `type uint64 = number /* || bigint */` and
// `type int64 = number` -- the BigInt arm is commented out because the
// declarations predate this fix. Every BigInt crossing that boundary is marked
// explicitly below rather than weakening the assertions with `any`.
import { Node, PackedByteArray, Resource, StreamPeerBuffer, instance_from_id, is_instance_id_valid } from "godot";
import { BIGINT_FOR_64BIT } from "godot-jsb";
import { reportTestFailure } from "../test-status";
import type { Numeric64 } from "../test-status";

/** 2^53-1: the largest integer a JS Number represents exactly. */
const MAX_SAFE = BigInt(Number.MAX_SAFE_INTEGER);

/** The `JSB_WITH_BIGINT` position this binary was built with. */
const BIGINT_MODE = BIGINT_FOR_64BIT;


/**
 * Typings boundary: a 64-bit slot is declared `number` but accepts a BigInt at
 * runtime (the conversion layer is what this task fixes). All call sites change
 * together once the declarations grow their BigInt arm.
 */
function asSlot(v: bigint): number {
	return v as unknown as number;
}

/**
 * Reinterpret any 64-bit result as its unsigned bit pattern, for comparison.
 *
 * Deliberately NOT `BigInt.asUintN`: quickjs-ng's implementation returns the
 * value unchanged when `bits >= JS_LIMB_BITS` (its "short bigint" is 32 bits
 * there), so `BigInt.asUintN(64, -1n)` yields `-1n` instead of
 * `18446744073709551615n` -- measured on the quickjs-ng leg, and it made this
 * file's bit comparisons report a false mismatch. A literal mod-2^64 mask is
 * the same arithmetic on every engine.
 */
const TWO_POW_64 = 1n << 64n;

function asUint64(v: bigint | Numeric64): bigint {
	const x = typeof v === "bigint" ? v : BigInt(v);
	return ((x % TWO_POW_64) + TWO_POW_64) % TWO_POW_64;
}

/**
 * The unsigned bit pattern a value leaves Godot as when `JSB_WITH_BIGINT`
 * is off: the engine writes `Number::New((double)v)`, so the low bits are gone.
 * `Number()` on these values is always integral, so `BigInt()` is safe.
 */
function lossy64(v: bigint): bigint {
	return ((BigInt(Number(v)) % TWO_POW_64) + TWO_POW_64) % TWO_POW_64;
}

/** Names the pad-to-16-digits hex form used in every failure detail. */
function hex64(v: bigint | Numeric64): string {
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
	{ label: "INT64_MIN", value: -BigInt("9223372036854775808") },
];

// Plain-number inputs that are exactly representable as a double. 2^63 is a
// power of two, so it survives the Number round-trip even though it is far
// above 2^53; 2^63+1 would not, so it is not listed here.
const NUMBER_FORM_CASES: SlotCase[] = [
	{ label: "2^53 (number)", value: BigInt("9007199254740992") },
	{ label: "2^63 (number)", value: BigInt("9223372036854775808") },
];

// Every assertion this scenario must make. Hardcoded (not derived from the case
// arrays) so that deleting a case shows up as a failure instead of silently
// shrinking the suite. The readback typeof/bit assertions are mode-dependent, so
// the two positions have different totals; the write section is identical in
// both (the switch only governs what leaves Godot).
//
// Narrow-slot range rejection is not asserted here: the reflect path never
// range-checked narrow metas (a pre-existing divergence, out of scope), so a
// leg-agnostic assertion would be red on `dynamic`. `test_jsb_int64_conv.h`
// covers `JSToGD<int8_t> / <uint8_t> / <char32_t>` at the converter instead.
// The BigInt round-trip sections (u64/i64 readback, u64 write, ObjectID) cannot
// run without BigInt in the build; only the narrow-slot truncation section is
// mode-independent, and it keeps its number-form case.
const EXPECTED_CHECKS = BIGINT_MODE ? 55 : 5;

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

/** Normalises a declared-64-bit return (`bigint` under the fixed switch) to a `number`. */

function asNumber(v: Numeric64): number {
	return typeof v === 'bigint' ? Number(v) : v;
}

/** Little-endian payload of a StreamPeerBuffer, folded back into one integer. */
function storedBits(bytes: PackedByteArray): bigint {
	let acc = 0n;
	// `size()` is a declared-64-bit return: `bigint` under the fixed switch.
	for (let i = asNumber(bytes.size()) - 1; i >= 0; i--) {
		acc = (acc << 8n) | BigInt(bytes.get(i) ?? 0);
	}
	return acc;
}

/**
 * Writes a uint64 through one leg and returns the bytes the engine stored.
 * Takes the typings' declared slot type (`number`); BigInt callers cross that
 * boundary through `asSlot`, so the cast stays visible at the call site.
 */
function writeU64(spb: StreamPeerBuffer, value: number, viaDynamic: boolean): bigint {
	spb.seek(0);
	if (viaDynamic) {
		spb.call("put_u64", value);
	} else {
		spb.put_u64(value);
	}
	return storedBits(spb.data_array);
}

/** Writes through the dynamic channel, then reads back through the static one. */
function readBackU64(spb: StreamPeerBuffer, value: bigint): Numeric64 {
	spb.seek(0);
	spb.call("put_u64", asSlot(value));
	spb.seek(0);
	return spb.get_u64();
}

/** Same for the signed slot. */
function readBackI64(spb: StreamPeerBuffer, value: bigint): Numeric64 {
	spb.seek(0);
	spb.call("put_64", asSlot(value));
	spb.seek(0);
	return spb.get_64();
}

export default class Int64 extends Node {
	_ready(): void {
		// A build without BigInt (`JSB_WITH_BIGINT=0`) cannot receive a BigInt
		// argument at all -- `probe_vt` has no BigInt branch and the readers have
		// no BigInt arm -- so the round-trip cases below cannot run there. The
		// narrow-slot and number-form cases are mode-independent and stay.
		if (!BIGINT_MODE) {
			console.warn(`INT64-DIAG skipped BigInt round-trip cases (JSB_WITH_BIGINT=0)`);
		}
		try {
			if (BIGINT_MODE) section("u64 readback", () => {
				const spb = new StreamPeerBuffer();
				for (const { label, value } of U64_CASES) {
					const got = readBackU64(spb, value);
					if (value > MAX_SAFE) {
						// AC1.2 / AC3.3: above the safe range it leaves as a BigInt
						// when the switch is on, and as the lossy Number when off.
						const expected = BIGINT_MODE ? "bigint" : "number";
						check(`u64 ${label} typeof`, typeof got === expected, `got ${typeof got}, want ${expected}`);
					}
					// AC1.2: and carry exactly the bits the slot holds -- the value
					// itself when the switch is on, its double rounding when off.
					const want = BIGINT_MODE ? value : lossy64(value);
					check(`u64 ${label} bits`, asUint64(got) === want, `${hex64(got)} != ${hex64(want)}`);
				}
			});

			if (BIGINT_MODE) section("i64 readback", () => {
				const spb = new StreamPeerBuffer();
				for (const { label, value } of I64_CASES) {
					const got = readBackI64(spb, value);
					// AC1.3: within the safe range it stays a Number; beyond it a
					// BigInt (switch on) or the lossy Number (switch off). The
					// signed direction is the one that used to be rounded
					// regardless of magnitude.
					const inSafeRange = value >= -MAX_SAFE;
					const expected = (inSafeRange || !BIGINT_MODE) ? "number" : "bigint";
					check(`i64 ${label} typeof`, typeof got === expected, `got ${typeof got}, want ${expected}`);
					const want = BIGINT_MODE ? asUint64(value) : lossy64(value);
					check(`i64 ${label} bits`, asUint64(got) === want, `${hex64(got)} != ${hex64(want)}`);
				}
			});

			if (BIGINT_MODE) section("u64 write", () => {
				// AC2.1 / AC3.2: the static leg used to reject everything >= 2^63 (a
				// `wide < 0 -> false` early return), while the dynamic vararg leg
				// wrote those same bytes. Both legs must now write `v mod 2^64`,
				// identically -- and the output switch must NOT affect this
				// direction, so the bytes are exact in both positions.
				const spb = new StreamPeerBuffer();
				for (const { label, value } of U64_CASES) {
					const viaStatic = writeU64(spb, asSlot(value), false);
					const viaDynamic = writeU64(spb, asSlot(value), true);
					check(`write ${label} static bits`, viaStatic === value, `${hex64(viaStatic)} != ${hex64(value)}`);
					check(`write ${label} dynamic bits`, viaDynamic === value, `${hex64(viaDynamic)} != ${hex64(value)}`);
					check(`write ${label} legs agree`, viaStatic === viaDynamic, `${hex64(viaStatic)} != ${hex64(viaDynamic)}`);
				}

				// AC2.3: the plain-number form, unchanged below the old rejection
				// point. Both values here are exactly representable as doubles.
				for (const { label, value } of NUMBER_FORM_CASES) {
					const asNumber = Number(value);
					const viaStatic = writeU64(spb, asNumber, false);
					const viaDynamic = writeU64(spb, asNumber, true);
					check(`write ${label} static bits`, viaStatic === value, `${hex64(viaStatic)} != ${hex64(value)}`);
					check(`write ${label} dynamic bits`, viaDynamic === value, `${hex64(viaDynamic)} != ${hex64(value)}`);
				}

				// AC2.2: a negative number wraps, exactly like the engine's own
				// `Variant::operator uint64_t()`.
				const negStatic = writeU64(spb, -1, false);
				const negDynamic = writeU64(spb, -1, true);
				check("write -1 static bits", negStatic === asUint64(-1), `${hex64(negStatic)}`);
				check("write -1 dynamic bits", negDynamic === asUint64(-1), `${hex64(negDynamic)}`);

				// 1e19 exceeds int64 but fits uint64; the double is exact enough
				// that the stored bytes are deterministic and equal on both legs.
				const bigStatic = writeU64(spb, 1e19, false);
				const bigDynamic = writeU64(spb, 1e19, true);
				check("write 1e19 static bits", bigStatic === BigInt("10000000000000000000"), hex64(bigStatic));
				check("write 1e19 dynamic bits", bigDynamic === BigInt("10000000000000000000"), hex64(bigDynamic));
			});

			section("narrow slots truncate like the engine", () => {
				// The engine never range-checks a narrow parameter: `put_8(300)`
				// writes 44 in plain GDScript (measured). Both binding legs must
				// follow -- the dynamic leg's class-method path IS the engine's own
				// conversion -- so these assert the stored bytes, not a throw.
				//
				// `writeU64` writes a full 8 bytes; these use the narrow writers, so
				// the payload is read back with the matching getter.
				const spb = new StreamPeerBuffer();

				// put_8(300) -> 300 mod 256 = 44 (signed 8-bit).
				spb.seek(0);
				spb.put_8(300);
				spb.seek(0);
				check("put_8(300) truncates", spb.get_8() === 44, `got ${String(spb.get_8())}`);

				// put_8(-129) -> 127.
				spb.seek(0);
				spb.put_8(-129);
				spb.seek(0);
				check("put_8(-129) truncates", spb.get_8() === 127, `got ${String(spb.get_8())}`);

				// put_u8(-1) -> 255 (unsigned view).
				spb.seek(0);
				spb.put_u8(-1);
				spb.seek(0);
				check("put_u8(-1) truncates", spb.get_u8() === 255, `got ${String(spb.get_u8())}`);

				// put_u16(70000) -> 4464.
				spb.seek(0);
				spb.put_u16(70000);
				spb.seek(0);
				check("put_u16(70000) truncates", spb.get_u16() === 4464, `got ${String(spb.get_u16())}`);

				// A BigInt narrows the same way (same slot, same cast) -- but a
				// build without BigInt cannot receive one.
				if (BIGINT_MODE) {
					spb.seek(0);
					spb.put_8(asSlot(300n));
					spb.seek(0);
					check("put_8(300n) truncates", spb.get_8() === 44, `got ${String(spb.get_8())}`);
				}

				// The dynamic channel must agree byte for byte.
				spb.seek(0);
				spb.call("put_8", 300);
				spb.seek(0);
				check("put_8(300) dynamic agrees", spb.get_8() === 44, `got ${String(spb.get_8())}`);
			});

			if (BIGINT_MODE) section("ObjectID round-trip", () => {
				// AC1.1: the ordinary handle round-trip has to work. `this` is a
				// live Node already in the tree, so the case needs no allocation
				// and no explicit free. A non-RefCounted id is small, so it is a
				// Number under the value-dependent switch -- but a DECLARED 64-bit
				// return of a small id is a Number (magnitude decides, not width).
				const selfId: Numeric64 = this.get_instance_id();
				check("Node id typeof", typeof selfId === "number", `got ${typeof selfId}`);
				check("Node id valid", is_instance_id_valid(asSlot(BigInt(selfId))));
				check("Node id round-trip", instance_from_id(asSlot(BigInt(selfId))) === this);

				// RefCounted ids set bit 63 (`is_ref_counted`), so this is the case
				// that used to be silently corrupted.
				const res = new Resource();
				const resId: Numeric64 = res.get_instance_id();
				if (BIGINT_MODE) {
					// AC1.4: it must leave unsigned, i.e. a positive BigInt.
					check("Resource id typeof", typeof resId === "bigint", `got ${typeof resId}`);
					check("Resource id positive", typeof resId === "bigint" && resId > 0n, `got ${String(resId)}`);
					check("Resource id bit63", asUint64(resId) >> 63n === 1n, `bits ${hex64(resId)}`);
					check("Resource id valid", is_instance_id_valid(asSlot(asUint64(resId))));
					check("Resource id round-trip", instance_from_id(asSlot(asUint64(resId))) === res);

					// Independently confirmed against `to_string()`, which formats
					// the id through `itos` -- a path with no double in it, so it is
					// the lossless reference. `itos` prints the *signed* view while
					// `resId` is now unsigned, so both sides are normalized to their
					// bit pattern before comparing.
					const exactText: string = res.to_string();
					const exactId = exactText.replace(/^.*#/, "").replace(/>$/, "");
					check("Resource id exact bits", asUint64(BigInt(exactId)) === asUint64(resId), `${hex64(BigInt(exactId))} != ${hex64(resId)}`);
				} else {
					// AC3.3: with the switch off the id leaves as a `Number`, so it
					// is lossy and the round trip is no longer guaranteed (that is
					// the pre-BigInt behaviour, and AC3.2 allows it to fail). The
					// assertion is therefore only the representation.
					check("Resource id typeof", typeof resId === "number", `got ${typeof resId}`);
				}
			});
		} finally {
			// No quit() here: start.ts owns scene teardown and the completion
			// sentinel. Quitting from the scene would kill the engine before the
			// sentinel is printed, and the suite would look like it never ran.
			if (checks !== EXPECTED_CHECKS) {
				reportTestFailure("int64 coverage", `${String(checks)} checks ran, expected exactly ${String(EXPECTED_CHECKS)}`);
			}
			console.warn(`INT64-DIAG checks=${String(checks)} expected=${String(EXPECTED_CHECKS)} bigint=${String(BIGINT_MODE)}`);
		}
	}
}
