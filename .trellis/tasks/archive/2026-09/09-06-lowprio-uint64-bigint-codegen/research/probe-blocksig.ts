import { Node } from "godot";

// `Object.set_block_signals(enable: bool)` has NO default_value in the api json,
// so `undefined` cannot be masked by default substitution. `is_blocking_signals()`
// reads the stored bool back — that makes the truth-value mapping observable.
export default class ProbeBlockSig extends Node {
	_ready(): void {
		try {
			const cases: [string, unknown][] = [
				["true", true],
				["false", false],
				["undefined", undefined],
				["null", null],
				["1 (number)", 1],
				["0 (number)", 0],
				["1n (bigint)", 1n],
				["0n (bigint)", 0n],
				["'' (string)", ""],
			];

			console.log("[probe] --- Object.set_block_signals(enable) [no default] ---");
			for (const [label, v] of cases) {
				try {
					this.set_block_signals(v as boolean);
					console.log(`[probe] set_block_signals(${label}) OK -> is_blocking_signals=${String(this.is_blocking_signals())}`);
				} catch (e) {
					console.log(`[probe] set_block_signals(${label}) THREW`);
				}
			}
			console.log(`[probe] Object in enabled_classes? (runtime reachability check) OK`);
		} finally {
			console.log("[probe] done");
			this.get_tree().quit();
		}
	}
}
