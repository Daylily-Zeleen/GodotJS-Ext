import { readFileSync } from "node:fs";
import { execFileSync } from "node:child_process";
import { getVersion } from "./get-version.js";

function hasCurrentVersionEntry(version) {
  let changelog;
  try {
    changelog = readFileSync("CHANGELOG.md", "utf-8");
  } catch {
    return false;
  }

  const changelogVersion = version.replace(/^v/, "");
  const headingPattern = new RegExp(`^##\\s+(?:\\[)?${changelogVersion.replace(/[.*+?^${}()|[\\]\\\\]/g, "\\\\$&")}(?:\\])?\\s*$`, "m");
  return headingPattern.test(changelog);
}

function releaseExists(version) {
  try {
    execFileSync("gh", ["release", "view", version], {
      stdio: ["ignore", "ignore", "pipe"],
    });
    return true;
  } catch (error) {
    const stderr = error?.stderr?.toString() ?? "";
    if (/not found|404/i.test(stderr)) {
      return false;
    }
    throw error;
  }
}

function shouldPublish() {
  const version = getVersion();

  if (!hasCurrentVersionEntry(version) || releaseExists(version)) {
    console.log("false");
    return;
  }

  console.log("true");
}

shouldPublish();
