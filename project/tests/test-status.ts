import { BIGINT_FOR_64BIT } from "godot-jsb";
import type { int64, uint64 } from "godot";

/**
 * Any 64-bit slot value, for test plumbing only.
 *
 * The generated `int64` / `uint64` aliases are separate types inside `declare
 * module "godot"`, so neither is assignable to the other; the suite needs one
 * name for "whatever a 64-bit slot hands back". With BigInt in the build that is
 * `number | bigint`; counts, ids and elapsed times stay `number` at either
 * position, which is why the arithmetic helpers normalise through `asNumber`.
 */
export const BIGINT_MODE: boolean = BIGINT_FOR_64BIT;

/**
 * Either 64-bit slot value, for test plumbing.
 *
 * `int64` and `uint64` are two distinct aliases, so neither is assignable to the
 * other and the suite needs one name for "whatever a 64-bit slot hands back".
 * Both are generated per build (`number | bigint`, or a plain `number` when the
 * build has no BigInt), so this stays correct in either position instead of
 * hard-coding one of them.
 */
export type Numeric64 = int64 | uint64;

export const TEST_COMPLETION_SENTINEL = "GODOTJS_TEST_PROJECT_COMPLETED";
export const TEST_FAILURE_SENTINEL_PREFIX = "GODOTJS_TEST_PROJECT_FAILED:";

let firstFailureContext: string | null = null;
let activeAsyncTests = 0;

function formatError(error: unknown): string {
	if (error instanceof Error) {
		return error.stack ?? error.message;
	}
	if (typeof error === "string") {
		return error;
	}
	try {
		return JSON.stringify(error);
	} catch {
		return String(error);
	}
}

export function hasTestFailure(): boolean {
	return firstFailureContext !== null;
}

export function beginAsyncTest(): void {
	activeAsyncTests += 1;
}

export function endAsyncTest(): void {
	if (activeAsyncTests > 0) {
		activeAsyncTests -= 1;
	}
}

export function hasActiveAsyncTests(): boolean {
	return activeAsyncTests > 0;
}

export function getActiveAsyncTestCount(): number {
	return activeAsyncTests;
}

export function reportTestFailure(context: string, error?: unknown): void {
	if (firstFailureContext !== null) {
		return;
	}
	firstFailureContext = context;
	console.error(`${TEST_FAILURE_SENTINEL_PREFIX} ${context}`);
	if (error !== undefined) {
		console.error(formatError(error));
	}
}

export function reportTestCompleted(): void {
	if (firstFailureContext !== null) {
		return;
	}
	console.warn(TEST_COMPLETION_SENTINEL);
}
