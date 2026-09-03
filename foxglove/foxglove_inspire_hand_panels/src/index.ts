import type { ExtensionContext } from "@foxglove/extension";

import { registerJointStateCombined } from "./converters/joint_state_combined";
import { registerPncTactileCombined } from "./converters/pnc_tactile_combined";
import { initDexhandConsole } from "./panels/dexhand_console";
import { initDexhandForce } from "./panels/dexhand_force";
import { initDexhandGripper } from "./panels/dexhand_gripper";
import { initPncTactile } from "./panels/pnc_tactile";

// Entry point. Foxglove invokes `activate` once when the extension is loaded.
//
// Phase B (current): register topic converters that replace the in-layout
// user-scripts. Built-in panels can then address fields by name through the
// new structured topics.
//
// Future phases will additionally register custom panels from this same
// `activate` function via `extensionContext.registerPanel(...)`.

export function activate(extensionContext: ExtensionContext): void {
  registerJointStateCombined(extensionContext);
  registerPncTactileCombined(extensionContext);

  extensionContext.registerPanel({
    name: "Dexterous Hand Console",
    initPanel: initDexhandConsole,
  });

  extensionContext.registerPanel({
    name: "Dexterous Hand Force",
    initPanel: initDexhandForce,
  });

  extensionContext.registerPanel({
    name: "Dexterous Hand Gripper",
    initPanel: initDexhandGripper,
  });

  extensionContext.registerPanel({
    name: "PNC Tactile Diagnostics",
    initPanel: initPncTactile,
  });
}
