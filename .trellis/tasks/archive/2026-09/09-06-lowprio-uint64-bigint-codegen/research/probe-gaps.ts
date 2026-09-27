import { Node, RandomNumberGenerator, StreamPeerBuffer, Vector2i } from "godot";

const hex64 = (v: bigint): string => `0x${BigInt.asUintN(64, v).toString(16).padStart(16, "0")}`;
const asU64 = (v: number): bigint => BigInt.asUintN(64, BigInt(v));

export default class ProbeGaps extends Node {
	_ready(): void {
		try {
			// A) signed int64 return, large negative: same lossy path as uint64 high values?
			const spb = new StreamPeerBuffer();
			spb.seek(0);
			spb.put_64(-9223372036854775807 as unknown as number); // INT64_MIN+1, not a power of two
			spb.seek(0);
			const got64 = spb.get_64();
			console.log(`[probe] int64 return INT64_MIN+1: typeof=${typeof got64} value=${String(got64)} bits=${hex64(asU64(got64 as number))} expected=0x8000000000000001 exact=${String(asU64(got64 as number) === 0x8000000000000001n)}`);

			// B) builtin constructor with a BigInt argument (probe_vt path).
			try {
				const v = new Vector2i(2n as unknown as number, 3);
				console.log(`[probe] new Vector2i(BigInt 2, 3) ok: ${String(v)}`);
			} catch (e) {
				console.log(`[probe] new Vector2i(BigInt 2, 3) REJECTED: ${String(e)}`);
			}
			try {
				const v = new Vector2i(2, 3);
				console.log(`[probe] new Vector2i(number 2, 3) ok: ${String(v)}`);
			} catch (e) {
				console.log(`[probe] new Vector2i(number 2, 3) REJECTED: ${String(e)}`);
			}

			// C) operator with a BigInt operand (probe_vt path).
			try {
				const r = (2 as unknown as { add(o: unknown): unknown }).add(2n);
				console.log(`[probe] number.add(BigInt) ok: ${String(r)}`);
			} catch (e) {
				console.log(`[probe] number.add(BigInt) threw: ${String(e).slice(0, 120)}`);
			}

			// D) RNG state property setter with a plain number above 2^53 (typings' only form).
			const rng = new RandomNumberGenerator();
			rng.state = 9223372036854775808;
			console.log(`[probe] rng.state = 2^63 (number) -> bits=${hex64(asU64(rng.state))} expected=0x8000000000000000 exact=${String(asU64(rng.state) === 0x8000000000000000n)}`);
		} finally {
			console.log("[probe] done");
			this.get_tree().quit();
		}
	}
}