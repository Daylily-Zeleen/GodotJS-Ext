// Worker startup script: throws while loading, proving that a failed entry
// script reaches the host's `onerror` instead of leaving `onready` pending.
// Scenario 3 of the cross-isolate error-reporting suite; worker-only (see the
// NOTE on `TestCrossEnvironment.runWorkerStartupLoadFailure`).
throw new Error("cross-environment-test: peer load throw");
