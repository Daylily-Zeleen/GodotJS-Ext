/**
 * Benchmark harness: measures JS -> Godot call overhead per case.
 *
 * Methodology:
 *  - probe each case once; cases that throw are reported invalid and skipped
 *  - warmup (JIT + engine caches), then per-case iteration calibration so a
 *    single timed round lasts ~TARGET_MS
 *  - ROUNDS timed rounds, reported metric is the median ns/call
 *  - a checksum of call results is accumulated and printed to defeat dead
 *    code elimination
 *
 * Results are printed as a single line prefixed with BENCH_JSON for the CI
 * benchmark job to collect.
 */
import { Engine, Node, Time, Vector2 } from "godot";
import { BUILTIN_CASES } from "./cases.builtin";
import { OBJECT_CASES } from "./cases.object";

export interface BuiltinCase {
	group: string;
	makeTarget: () => any;
	cases: { name: string; fn: (t: any) => any }[];
}

interface CaseResult {
	name: string;
	nsPerCall: number;
	iterations: number;
	error?: string;
}

interface BenchOutcome {
	nsPerCall: number;
	iterations: number;
	checksum: number;
	error?: string;
}

// jsb's v8 environment has no global `performance`; use the engine
// monotonic microsecond clock instead (finer grained anyway).
const nowMs = (): number => Time.get_ticks_usec() / 1000;

const WARMUP_CALLS = 200;
const ROUNDS = 7;
const TARGET_MS_PER_ROUND = 120;
const MAX_ITERATIONS = 1 << 22;

function median(samples: number[]): number {
	const s = [...samples].sort((a, b) => a - b);
	const mid = s.length >> 1;
	return s.length % 2 ? s[mid] : (s[mid - 1] + s[mid]) / 2;
}

function calibrate(fn: () => any): number {
	let iterations = 64;
	for (;;) {
		const t0 = nowMs();
		for (let i = 0; i < iterations; i++) fn();
		const elapsed = nowMs() - t0;
		if (elapsed >= TARGET_MS_PER_ROUND / 2 || iterations >= MAX_ITERATIONS) {
			return iterations;
		}
		const scaled = Math.ceil((iterations * TARGET_MS_PER_ROUND) / Math.max(elapsed, 0.01) / 2);
		iterations = Math.min(MAX_ITERATIONS, Math.max(iterations * 2, scaled));
	}
}

function benchOne(fn: () => any): BenchOutcome {
	// probe: a throwing case is invalid for this binding configuration
	try {
		fn();
	} catch (e: any) {
		return { nsPerCall: 0, iterations: 0, checksum: 0, error: String(e?.message ?? e) };
	}

	for (let i = 0; i < WARMUP_CALLS; i++) fn();

	const iterations = calibrate(fn);
	const samples: number[] = [];
	let checksum = 0;
	for (let r = 0; r < ROUNDS; r++) {
		const t0 = nowMs();
		for (let i = 0; i < iterations; i++) {
			// accumulate a weak fingerprint of every result; calls are never
			// pure, but this makes DCE obvious if it ever happens
			const v = fn();
			checksum += v === undefined || v === null ? 0 : 1;
		}
		samples.push(((nowMs() - t0) * 1e6) / iterations);
	}
	return { nsPerCall: Math.round(median(samples) * 10) / 10, iterations, checksum };
}

export default class Benchmark extends Node {
	async _ready() {
		const staticBinding = (Vector2 as any).IN !== undefined;
		const results: CaseResult[] = [];
		let checksum = 0;

		// global engine/JIT warmup before any measurement
		{
			const a = new Vector2(1, 2);
			for (let i = 0; i < 5000; i++) a.length();
		}

		const runGroup = (group: string, makeTarget: () => any, cases: { name: string; fn: (t: any) => any }[]) => {
			let target: any;
			try {
				target = makeTarget();
			} catch (e: any) {
				for (const c of cases) {
					results.push({ name: `${group}.${c.name}`, nsPerCall: 0, iterations: 0, error: "target: " + String(e?.message ?? e) });
				}
				return;
			}
			for (const c of cases) {
				const r = benchOne(() => c.fn(target));
				checksum += r.checksum;
				if (r.error) {
					results.push({ name: `${group}.${c.name}`, nsPerCall: 0, iterations: 0, error: r.error });
				} else {
					results.push({ name: `${group}.${c.name}`, nsPerCall: r.nsPerCall, iterations: r.iterations });
				}
			}
		};

		for (const g of BUILTIN_CASES) {
			runGroup(g.group, g.makeTarget, g.cases);
		}
		for (const g of OBJECT_CASES) {
			runGroup(g.group, g.makeTarget, g.cases);
		}

		const report = {
			staticBinding,
			engine: Engine.get_version_info().full_name,
			checksum,
			invalid: results.filter((r) => r.error).length,
			results,
		};
		console.log("BENCH_JSON " + JSON.stringify(report));
		this.get_tree()?.quit(0);
	}
}
