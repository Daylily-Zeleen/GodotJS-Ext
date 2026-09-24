import { Node, RandomNumberGenerator } from "godot";

const hex64 = (v: bigint): string => `0x${BigInt.asUintN(64, v).toString(16).padStart(16, "0")}`;
const asU64 = (v: number): bigint => BigInt.asUintN(64, BigInt(v));

export default class ProbeProp extends Node {
	_ready(): void {
		try {
			const rng = new RandomNumberGenerator();
			// Same logical operation, same input type (plain number), two spellings.
			const inputs: [string, number][] = [
				["2^53", 9007199254740992],
				["2^62", 4611686018427387904],
				["2^63", 9223372036854775808],
				["1e19", 10000000000000000000],
			];
			for (const [name, v] of inputs) {
				try {
					rng.state = v;
					console.log(`[probe] property  rng.state = ${name}  -> OK, readback bits=${hex64(asU64(rng.state))}`);
				} catch (e) {
					console.log(`[probe] property  rng.state = ${name}  -> THREW: ${String(e).slice(0, 90)}`);
				}
				try {
					rng.set_state(v);
					console.log(`[probe] method    rng.set_state(${name}) -> OK, readback bits=${hex64(asU64(rng.get_state()))}`);
				} catch (e) {
					console.log(`[probe] method    rng.set_state(${name}) -> THREW: ${String(e).slice(0, 90)}`);
				}
			}
		} finally {
			console.log("[probe] done");
			this.get_tree().quit();
		}
	}
}