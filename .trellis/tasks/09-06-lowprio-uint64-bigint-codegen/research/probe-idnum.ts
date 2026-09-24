import { Node, Resource, instance_from_id, is_instance_id_valid } from "godot";

// Numbers only. The documented ObjectID round trip, exactly as a normal user writes it.
export default class ProbeIdNum extends Node {
	_ready(): void {
		try {
			const plain = new Node();
			const plainId = plain.get_instance_id();
			console.log(`[probe] plain Node: id=${String(plainId)} typeof=${typeof plainId} valid=${String(is_instance_id_valid(plainId))} roundTrip=${String(instance_from_id(plainId) === plain)}`);

			const res = new Resource();
			const resId = res.get_instance_id();
			console.log(`[probe] refcounted Resource: id=${String(resId)} typeof=${typeof resId} valid=${String(is_instance_id_valid(resId))} roundTrip=${String(instance_from_id(resId) === res)}`);

			// Realistic pattern: keep the id, drop the object, look it up later.
			let saved = new Resource().get_instance_id();
			console.log(`[probe] saved refcounted id=${String(saved)} lookup=${String(instance_from_id(saved) === null ? "null" : "object")} valid=${String(is_instance_id_valid(saved))}`);

			// Non-refcounted sibling for contrast.
			let savedNode = new Node().get_instance_id();
			console.log(`[probe] saved Node id=${String(savedNode)} lookupNull=${String(instance_from_id(savedNode) === null)} valid=${String(is_instance_id_valid(savedNode))}`);
		} finally {
			console.log("[probe] done");
			this.get_tree().quit();
		}
	}
}