import { Node, PackedByteArray, Resource, StreamPeerBuffer, instance_from_id, is_instance_id_valid } from "godot";

// The generated typings declare `type uint64 = number /* || bigint */` — the BigInt
// arm is commented out. The runtime conversion layer does accept BigInt for int64/
// uint64 slots (that is what this probe measures), so the cast below marks the
// boundary where a BigInt is fed to a slot the typings type as `number`.
function asU64(v: bigint): number {
	return v as unknown as number;
}

const hex64 = (v: bigint): string => `0x${BigInt.asUintN(64, v).toString(16).padStart(16, "0")}`;

const asUint64 = (v: number): bigint => BigInt.asUintN(64, BigInt(v));

// StreamPeerBuffer stores little-endian; fold the payload back into one integer.
function storedBits(bytes: PackedByteArray): bigint {
	let acc = 0n;
	for (let i = bytes.size() - 1; i >= 0; i--) acc = (acc << 8n) | BigInt(bytes.get(i) ?? 0);
	return acc;
}

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

			for (const [name, v] of cases) {
				spb.seek(0);
				try {
					spb.put_u64(asU64(v));
					const stored = storedBits(spb.data_array);
					console.log(`[probe] STATIC put_u64 ${name}: sent=${hex64(v)} stored=${hex64(stored)} writeExact=${String(stored === v)}`);
				} catch (e) {
					console.log(`[probe] STATIC put_u64 ${name} REJECTED: ${String(e).slice(0, 96)}`);
				}
			}
			for (const [name, v] of cases) {
				spb.seek(0);
				spb.call("put_u64", asU64(v));
				const stored = storedBits(spb.data_array);
				console.log(`[probe] DYNAMIC put_u64 ${name}: sent=${hex64(v)} stored=${hex64(stored)} writeExact=${String(stored === v)}`);
			}

			for (const [name, v] of cases) {
				spb.seek(0);
				spb.call("put_u64", asU64(v));
				spb.seek(0);
				const got = spb.get_u64();
				console.log(`[probe] RETURN get_u64 ${name}: engineBits=${hex64(v)} typeof=${typeof got} value=${String(got)} jsBits=${hex64(asUint64(got as number))} readExact=${String(asUint64(got as number) === v)}`);
			}

			const node = new Node();
			const nodeId = node.get_instance_id();
			console.log(`[probe] Node.get_instance_id: typeof=${typeof nodeId} bits=${hex64(asUint64(nodeId))} roundTrip=${String(instance_from_id(nodeId) === node)} valid=${String(is_instance_id_valid(nodeId))}`);

			const res = new Resource();
			const resId = res.get_instance_id();
			console.log(`[probe] Resource.get_instance_id: typeof=${typeof resId} bits=${hex64(asUint64(resId))} roundTrip=${String(instance_from_id(resId) === res)} valid=${String(is_instance_id_valid(resId))}`);
		} finally {
			console.log("[probe] done");
			this.get_tree().quit();
		}
	}
}
