import { execSync } from "child_process";
import { pathToFileURL } from "node:url";

export const getVersion = () => {
  const raw = execSync("pnpm pkg get version").toString().trim();
  // pnpm prints a JSON string for `pkg get` in some versions and a bare value
  // in others; accept both so the tag can never come out as `v"1.0.1"`.
  const version = raw.startsWith('"') ? JSON.parse(raw) : raw;
  return `v${version}`;
};

// `getVersion()` only returns the value; the release workflows also run this
// file as a program (`version="$(node release/get-version.js)"`) and want the
// tag on stdout. Without this guard the module was imported, the export was
// never called, and the command printed nothing - so the tag expanded to the
// empty string. The empty tag then reached `gh release view ""` in
// should-publish.js, whose `not found` branch reads as "this release does not
// exist yet", so the run took the publish path, created a release, and the
// upload jobs died with `HttpError: Not Found` while looking for tag "".
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  console.log(getVersion());
}
