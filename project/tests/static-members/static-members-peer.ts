import { JSWorkerParent } from "godot.worker";
import StaticMembersTarget from "./static-members-target";

// Worker side of the cross-environment check: this isolate imports the very same module, so the
// class parse installs its own accessors - they must resolve against the one process-wide store,
// not a per-isolate copy. The main environment drives this peer with plain action strings.
if (JSWorkerParent) {
  JSWorkerParent.onmessage = (action: unknown) => {
    if (action === "read") {
      JSWorkerParent!.postMessage({ action, value: StaticMembersTarget.score });
    } else if (action === "write") {
      StaticMembersTarget.score = 2000;
      JSWorkerParent!.postMessage({ action, value: StaticMembersTarget.score });
    }
  };
}
