// Output schemas for the topic converters in this extension.
//
// Foxglove understands the structure described here via the
// `outputSchemaDescription` argument of `registerMessageConverter` and uses it
// to populate path autocompletion in built-in panels (Plot, RawMessages, etc.).
//
// Keep these names unique inside the workspace so they don't collide with any
// schema delivered by the live ROS 2 connection.

import type { MessageSchemaDescription } from "@foxglove/extension";

// ---------------------------------------------------------------------------
// /inspire_hand/joints  (from /dynamic_joint_states)
// ---------------------------------------------------------------------------

export const HAND_JOINTS_SCHEMA_NAME = "renesas.DexterousHandJoints";

export const HAND_JOINTS_SCHEMA: MessageSchemaDescription = {
  joints: [
    {
      name: "string",
      position: "number",
      velocity: "number",
      force: "number",
    },
  ],
};

export type HandJointEntry = {
  name: string;
  position: number;
  velocity: number;
  force: number;
};

export type HandJointsMessage = {
  joints: HandJointEntry[];
};

// ---------------------------------------------------------------------------
// PNC raw electrical slots; unavailable numbers stay NaN, with explicit known flags.
export const PNC_TACTILE_SCHEMA_NAME = "renesas.PncTactileChannels";
export const PNC_TACTILE_SCHEMA: MessageSchemaDescription = {
  names_received: "bool",
  channels: [{ name: "string", raw_i: "number", raw_q: "number", value: "number",
    raw_i_known: "bool", raw_q_known: "bool", value_known: "bool" }],
};
