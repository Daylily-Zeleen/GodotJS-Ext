import { Node, Vector2, Vector2i } from "godot";

export default class ProbeOps extends Node {
	_ready(): void {
		try {
			const v2 = new Vector2(1, 2);

			// Operator with a BigInt right operand (int64 slot: Vector2.OP_MULTIPLY(int64_t)).
			try {
				const r = v2.OP_MULTIPLY(2n as unknown as number) as unknown as { x: number; y: number };
				console.log(`[probe] Vector2.OP_MULTIPLY(2n) OK -> x=${String(r.x)} y=${String(r.y)} (expect x=2 y=4)`);
			} catch (e) {
				console.log(`[probe] Vector2.OP_MULTIPLY(2n) THREW: ${String(e).slice(0, 110)}`);
			}
			try {
				const r = v2.OP_MULTIPLY(2) as unknown as { x: number; y: number };
				console.log(`[probe] Vector2.OP_MULTIPLY(2) OK -> x=${String(r.x)} y=${String(r.y)} (expect x=2 y=4)`);
			} catch (e) {
				console.log(`[probe] Vector2.OP_MULTIPLY(2) THREW: ${String(e).slice(0, 110)}`);
			}
			try {
				const r = v2.OP_ADD(new Vector2(3, 4));
				console.log(`[probe] Vector2.OP_ADD(Vector2) OK -> ${String(r)}`);
			} catch (e) {
				console.log(`[probe] Vector2.OP_ADD(Vector2) THREW: ${String(e).slice(0, 110)}`);
			}

			// Constructors with a BigInt argument.
			for (const [name, fn] of [
				["new Vector2i(2n, 3)", () => new Vector2i(2n as unknown as number, 3)],
				["new Vector2i(2, 3)", () => new Vector2i(2, 3)],
				["new Vector2(2n, 3n)", () => new Vector2(2n as unknown as number, 3n as unknown as number)],
			] as [string, () => unknown][]) {
				try {
					console.log(`[probe] ${name} OK -> ${String(fn())}`);
				} catch (e) {
					console.log(`[probe] ${name} THREW: ${String(e).slice(0, 110)}`);
				}
			}
		} finally {
			console.log("[probe] done");
			this.get_tree().quit();
		}
	}
}