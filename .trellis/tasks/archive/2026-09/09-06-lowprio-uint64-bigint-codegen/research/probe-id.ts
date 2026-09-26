import { Node, Resource, instance_from_id, is_instance_id_valid } from "godot";

// Object.to_string() prints "#<itos(get_instance_id())>" — itos formats the full
// int64 exactly (no double in the path), so this is the only lossless view of a
// refcounted ObjectID the current binding exposes.
function exactIdText(o: object): string {
	const s = (o as { to_string(): string }).to_string();
	return s.replace(/^.*#/, "").replace(/>$/, "");
}

const hex64 = (v: bigint): string => `0x${BigInt.asUintN(64, v).toString(16).padStart(16, "0")}`;

export default class ProbeId extends Node {
	_ready(): void {
		try {
			const node = new Node();
			const nodeText = exactIdText(node);
			const nodeId = node.get_instance_id();
			console.log(`[probe] Node: exactText=${nodeText} exactBits=${hex64(BigInt(nodeText))} jsBits=${hex64(BigInt.asUintN(64, BigInt(nodeId)))} same=${String(BigInt(nodeText) === BigInt.asUintN(64, BigInt(nodeId)))}`);

			const res = new Resource();
			const resText = exactIdText(res);
			const resId = res.get_instance_id();
			console.log(`[probe] Resource: exactText=${resText} exactBits=${hex64(BigInt(resText))} jsBits=${hex64(BigInt.asUintN(64, BigInt(resId)))} same=${String(BigInt(resText) === BigInt.asUintN(64, BigInt(resId)))}`);

			// Feed the EXACT id as a BigInt into the int64-typed utility function.
			// This is the target semantics for a fixed return path: BigInt in, identity out.
			const viaExactBigInt = instance_from_id(BigInt(resText) as unknown as number);
			console.log(`[probe] instance_from_id(exact BigInt) same=${String(viaExactBigInt === res)} valid=${String(is_instance_id_valid(BigInt(resText) as unknown as number))}`);
			// ...and as the lossy Number the current return path yields.
			console.log(`[probe] instance_from_id(lossy number) same=${String(instance_from_id(resId) === res)}`);
		} finally {
			console.log("[probe] done");
			this.get_tree().quit();
		}
	}
}
