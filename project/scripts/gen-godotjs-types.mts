import { execFileSync } from 'node:child_process';
import { rmSync } from 'node:fs';
import { join } from 'node:path';

const testsProjectRoot = join(import.meta.dirname, '..');

const godotBinary = process.env.GODOT;

if (!godotBinary) {
  throw new Error('GODOT environment variable is required. Example: GODOT=/Applications/Godot47.app/Contents/MacOS/Godot');
}

const runTsc = () =>
  execFileSync('pnpm', ['exec', 'tsc'], {
    cwd: testsProjectRoot,
    stdio: 'inherit',
    shell: process.platform === 'win32',
    env: process.env,
  });

// Phase 1 — bootstrap emit. `--generate-types` resolves scene/resource scripts
// by reading their emitted JS, so the JS must exist before it runs. On a clean
// tree `typings/` (and therefore the `godot` module declarations) does not
// exist yet, so this pass cannot type-check; it exists only to emit. Its
// failures are expected and ignored — phase 3 is what proves the project
// type-checks.
try {
  runTsc();
} catch {
  console.warn('[gen-godotjs-types] bootstrap emit reported errors (expected before typings exist)');
}

// The editor is known to crash during its own shutdown/reimport phase *after*
// the types have been written (tracked separately; same tolerance as
// misc/verify_codegen.py). Success is judged by the artifacts below, not by
// the process exit code — so an exit here is not fatal.
try {
  execFileSync(
    godotBinary,
    ['--headless', '--editor', '--generate-types', '--path', testsProjectRoot],
    { stdio: 'inherit', timeout: 120_000 },
  );
} catch (e) {
  console.warn('[gen-godotjs-types] --generate-types exited non-zero (known shutdown crash); verifying artifacts');
  if (e && typeof e === 'object' && 'status' in e && e.status === null) throw e; // spawn failure, not a crash
}

// The placeholder file is useful for in-module compilation, but it conflicts with strict project checks.
rmSync(join(testsProjectRoot, 'typings', 'godot.generated.d.ts'), { force: true });

// Phase 3 — enforcing type-check. With typings in place this must be clean; a
// non-zero exit propagates and fails the command. If generation did not
// actually produce typings, this fails with "module 'godot' cannot be found".
runTsc();
