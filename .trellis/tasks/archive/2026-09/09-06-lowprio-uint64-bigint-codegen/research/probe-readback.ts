import { Node, StreamPeerBuffer } from "godot";

// The generated typings declare `type uint64 = number /* || bigint */` — the BigInt
// arm is commented out. The runtime conversion layer does accept BigInt for int64/
// uint64 slots (that is what this probe measures), so the cast below marks the
// boundary where a BigInt is fed to a slot the typings type as `number`.
function asU64(v: bigint): number {
	return v as unknown as number;
}

const hex64 = (v: bigint): string => `0x${BigInt.asUintN(64, v).toString(16).padStart(16, "0")}`;

const asUint64 = (v: number): bigint => BigInt.asUintN(64, BigInt(v));

export default class ProbeUint64 extends Node {
	_ready(): void {
		try {
			const spb = new StreamPeerBuffer();
			const cases: [string, bigint][] = [
				["2^53", BigInt(9007199254740992)],
				["2^63", BigInt("9223372036854775808")],
				["2^63+1", BigInt("9223372036854775809")],
				["2^64-1", BigInt("18446744073709551615")],
			];

			// Both legs get the identical engine-side value written by the dynamic
			// call (exact bytes), then read it back through each leg's own return path.
			for (const [name, v] of cases) {
				spb.seek(0);
				spb.call("put_u64", asU64(v));

				spb.seek(0);
				const viaStatic = spb.get_u64();
				spb.seek(0);
				const viaDynamic = spb.call("get_u64");
				console.log(`[probe] READBACK ${name} engineBits=${hex64(v)} | static: typeof=${typeof viaStatic} value=${String(viaStatic)} bits=${hex64(asUint64(viaStatic as number))} | dynamic: typeof=${typeof viaDynamic} value=${String(viaDynamic)} bits=${hex64(asUint64(viaDynamic as number))}`);
			}
		} finally {
			console.log("[probe] done");
			this.get_tree().quit();
		}
	}
}
