import { Node, StreamPeerBuffer } from "godot";

const hex64 = (v: bigint): string => `0x${BigInt.asUintN(64, v).toString(16).padStart(16, "0")}`;

// Read the 8 bytes actually written into the stream buffer (little-endian).
function storedBits(bytes: { size(): number; get(i: number): number | undefined }): bigint {
	let acc = 0n;
	for (let i = bytes.size() - 1; i >= 0; i--) acc = (acc << 8n) | BigInt(bytes.get(i) ?? 0);
	return acc;
}

export default class ProbeNum extends Node {
	_ready(): void {
		try {
			const spb = new StreamPeerBuffer();
			// Plain JS numbers only — no BigInt anywhere in this probe.
			const cases: [string, number][] = [
				["2^53-1", 9007199254740991],
				["2^53", 9007199254740992],
				["2^53+1", 9007199254740993],
				["2^63", 9223372036854775808],
				["2^63+1", 9223372036854775809],
				["2^64-1 literal (rounds to 2^64)", 18446744073709551615],
				["1e19", 10000000000000000000],
				["-1", -1],
				["-2^63", -9223372036854775808],
			];

			for (const [name, v] of cases) {
				spb.seek(0);
				try {
					spb.put_u64(v);
					const bits = storedBits(spb.data_array);
					console.log(`[probe] STATIC put_u64(number ${name}): inputAsDoubleBits=${hex64(BigInt.asUintN(64, BigInt(v)))} stored=${hex64(bits)} OK`);
				} catch (e) {
					console.log(`[probe] STATIC put_u64(number ${name}) THREW: ${String(e)}`);
				}
			}

			for (const [name, v] of cases) {
				spb.seek(0);
				try {
					spb.call("put_u64", v);
					const bits = storedBits(spb.data_array);
					console.log(`[probe] DYNAMIC put_u64(number ${name}): stored=${hex64(bits)} OK`);
				} catch (e) {
					console.log(`[probe] DYNAMIC put_u64(number ${name}) THREW: ${String(e)}`);
				}
			}

			// int64-meta slot (put_64), plain numbers.
			for (const [name, v] of [["-2^63", -9223372036854775808], ["2^63-1", 9223372036854775807], ["-2^63-1 literal (rounds to -2^63)", -9223372036854775809]] as [string, number][]) {
				spb.seek(0);
				try {
					spb.put_64(v);
					console.log(`[probe] STATIC put_64(number ${name}): stored=${hex64(storedBits(spb.data_array))} OK`);
				} catch (e) {
					console.log(`[probe] STATIC put_64(number ${name}) THREW: ${String(e)}`);
				}
			}
		} finally {
			console.log("[probe] done");
			this.get_tree().quit();
		}
	}
}