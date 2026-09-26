// uid://dxbhcskd6lnrf This line is generated, don't modify or remove it.
/**
 * Benchmark harness: measures each case lambda, including argument construction.
 *
 * Methodology:
 *  - probe each case once; cases that throw are reported invalid and skipped
 *  - warmup (JIT + engine caches), then calibrate until a batch takes at least
 *    TARGET_MS_PER_ROUND / 2, or reaches MAX_ITERATIONS
 *  - ROUNDS timed rounds, reported metric is the median ns/call
 *  - timed loops also count non-null results; this count is not in BENCH_JSON
 *
 * Results are printed as a single line prefixed with BENCH_JSON for the CI
 * benchmark job to collect.
 *
 * Invocation: `godot --headless --path . -- --bench [--gc] [--only=<group>]`.
 * The benchmark switches go after `--` (get_cmdline_user_args): --bench selects
 * the scene in start.ts, --gc requests GC before each case's probe and warmup,
 * and --only selects all entries with an exact matching group name.
 */
import { SceneTree, Engine, Node, OS, Time, Vector2 } from "godot";
import { BUILTIN_CASES } from "./cases.builtin";
import { OBJECT_CASES } from "./cases.object";
import { BINDING_MODE } from "godot-jsb";

import type { Numeric64 } from "../test-status";

export interface CaseGroup {
    group: string;
    makeTarget: () => any;
    /**
     * `processDependent` marks a case whose probe result is a property of the
     * *process* rather than of the binding path -- an ObjectID, for instance.
     * The static/dynamic legs run as two separate engine processes, so such a
     * value can never be equal between them; the CI consistency gate compares
     * `sample` across the legs and must skip these.
     */
    cases: { name: string; fn: (t: any) => any; processDependent?: boolean }[];
}

interface CaseResult {
    name: string;
    nsPerCall: number;
    iterations: number;
    sample?: string;
    error?: string;
    processDependent?: boolean;
}

// Probe-result fingerprint: primitive value, or constructor name for objects.
function summarize(v: any): string {
    if (v === null || v === undefined) return "null";
    const t = typeof v;
    if (t === "number") return "num:" + v;
    if (t === "boolean") return "bool:" + v;
    if (t === "string") return "str:" + v;
    if (t === "function") return "fn";
    const ctor = v.constructor?.name ?? "obj";
    return "obj:" + ctor;
}

interface BenchOutcome {
    nsPerCall: number;
    iterations: number;
    checksum: number;
    error?: string;
    sample?: string;
}

/** Normalises a declared-64-bit return (`bigint` under the fixed switch) to a `number`. */

function asNumber(v: Numeric64): number {
	return typeof v === 'bigint' ? Number(v) : v;
}

// Use the engine's monotonic microsecond clock without relying on performance.
// The generated 64-bit alias (`number | bigint`, or a plain `number` when the
// build has no BigInt); the writer picks by magnitude, so normalise first.
const nowMs = (): number => asNumber(Time.get_ticks_usec()) / 1000;

const _args_user = OS.get_cmdline_user_args();
const GC_REQUESTED = _args_user.has("--gc");

const WARMUP_CALLS = 100;
const ROUNDS = 11;
const TARGET_MS_PER_ROUND = 30;
const MAX_ITERATIONS = 1 << 22;

function median(samples: number[]): number {
    const s = [...samples].sort((a, b) => a - b);
    const mid = s.length >> 1;
    return s.length % 2 ? s[mid] : (s[mid - 1] + s[mid]) / 2;
}

function calibrate(fn: () => any): number {
    let iterations = 64;
    for (; ;) {
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

async function benchOne(fn: () => any): Promise<BenchOutcome> {
    // probe: a throwing case is invalid for this binding configuration
    let sample = "invalid";
    try {
        const v = fn();
        sample = summarize(v);
    } catch (e: any) {
        return { nsPerCall: 0, iterations: 0, checksum: 0, sample: "invalid", error: String(e?.message ?? e) };
    }

    for (let i = 0; i < WARMUP_CALLS; i++) fn();

    const iterations = calibrate(fn);
    const samples: number[] = [];
    let checksum = 0;
    for (let r = 0; r < ROUNDS; r++) {
        const t0 = nowMs();
        for (let i = 0; i < iterations; i++) {
            // Count non-null results alongside the timed calls.
            const v = fn();
            checksum += v === undefined || v === null ? 0 : 1;
        }
        samples.push(((nowMs() - t0) * 1e6) / iterations);

        await (Engine.get_main_loop() as SceneTree).process_frame.as_promise(); // yield between rounds; does not force GC
    }
    return { nsPerCall: Math.round(median(samples) * 10) / 10, iterations, checksum, sample };
}

export default class Benchmark extends Node {
    public completeCallback: (() => any) | null = null;

    async _ready() {
        const bindingMode = BINDING_MODE;
        const results: CaseResult[] = [];
        let checksum = 0;

        // --gc requests synchronous global gc() before each case's probe/warmup.
        // Without that binding, warn once and report gcRequested=false.
        const gcFn = (globalThis as { gc?: () => void }).gc;
        const gcAvailable = typeof gcFn === "function";
        if (GC_REQUESTED && !gcAvailable) {
            console.warn("BENCH WARNING: --gc requested but global gc() is not exposed on this build; ignoring");
        }
        const gcBeforeCase = (): void => {
            if (GC_REQUESTED && gcAvailable) gcFn!();
        };

        // global engine/JIT warmup before any measurement
        {
            const a = new Vector2(1, 2);
            for (let i = 0; i < 5000; i++) a.length();
        }

        const runGroup = async (group: string, makeTarget: () => any, cases: { name: string; fn: (t: any) => any; processDependent?: boolean }[]) => {
            let target: any;
            try {
                target = makeTarget();
            } catch (e: any) {
                for (const c of cases) {
                    results.push({
                        name: `${group}.${c.name}`,
                        nsPerCall: 0,
                        iterations: 0,
                        error: "target: " + String(e?.message ?? e),
                        processDependent: c.processDependent,
                    });
                }
                return;
            }
            for (const c of cases) {
                gcBeforeCase();
                const r = await benchOne(() => c.fn(target));
                checksum += r.checksum;
                if (r.error) {
                    results.push({
                        name: `${group}.${c.name}`,
                        nsPerCall: 0,
                        iterations: 0,
                        sample: r.sample,
                        error: r.error,
                        processDependent: c.processDependent,
                    });
                } else {
                    results.push({
                        name: `${group}.${c.name}`,
                        nsPerCall: r.nsPerCall,
                        iterations: r.iterations,
                        sample: r.sample,
                        processDependent: c.processDependent,
                    });
                }
                await this.get_tree().process_frame.as_promise();
            }
        };

        // --only=<group> selects matching builtin and object group entries.
        let _only: string | undefined;
        const _args = OS.get_cmdline_user_args();
        for (let i = 0; i < _args.size(); i++) {
            const a = _args.get(i);
            if (a.startsWith("--only=")) _only = a.split("=")[1];
        }
        let _subset = BUILTIN_CASES;
        if (_only) _subset = BUILTIN_CASES.filter((g: any) => g.group === _only);
        for (const g of _subset) {
            await runGroup(g.group, g.makeTarget, g.cases);
        }
        for (const g of OBJECT_CASES) {
            if (_only && g.group !== _only) continue;
            await runGroup(g.group, g.makeTarget, g.cases);
        }

        const report = {
            bindingMode,
            gcRequested: GC_REQUESTED && gcAvailable,
            // GDictionary access needs no cast; "string" is the engine's own
            // full version string (e.g. "4.7.2.stable.official."+hash).
            engine: String(Engine.get_version_info().get("string")),
            invalid: results.filter((r) => r.error).length,
            results,
        };
        console.warn("BENCH_JSON " + JSON.stringify(report));
        // start.ts owns shutdown after completeCallback; do not quit here.

        if (this.completeCallback) this.completeCallback();
    }
}
