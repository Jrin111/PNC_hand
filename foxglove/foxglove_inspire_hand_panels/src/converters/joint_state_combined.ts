import type {
  ExtensionContext,
  Immutable,
  MessageEvent,
} from "@foxglove/extension";

import {
  HAND_JOINTS_SCHEMA,
  HAND_JOINTS_SCHEMA_NAME,
  type HandJointEntry,
  type HandJointsMessage,
} from "../schemas/messages";

// ---------------------------------------------------------------------------
// Topic converter:  /dynamic_joint_states  ->  /inspire_hand/joints
//
// Replaces the `hand_joint_state_flat` user-script. Instead of a flat
// Float64MultiArray addressed by `data[j*3+i]`, this emits a structured
// message that built-in Plot / RawMessages panels can address by joint name:
//
//   /inspire_hand/joints.joints[?name=="thumb_proximal_pitch_joint"].force
//
// The output ordering follows the JOINT_ORDER constant below so downstream
// panels see a stable index regardless of the controller_manager publish
// order in any given frame.
// ---------------------------------------------------------------------------

const JOINT_ORDER = [
  "thumb_proximal_yaw_joint",
  "thumb_proximal_pitch_joint",
  "index_proximal_joint",
  "middle_proximal_joint",
  "ring_proximal_joint",
  "pinky_proximal_joint",
] as const;

// Standard ros2_control state interfaces we expose. Anything else in the
// incoming message is dropped on the floor — these are the only quantities the
// downstream UI consumes today.
const POSITION = "position";
const VELOCITY = "velocity";
const FORCE = "force";

type InterfaceValue = {
  interface_names: string[];
  values: number[] | Float64Array;
};

type DynamicJointState = {
  joint_names: string[];
  interface_values: InterfaceValue[];
};

const INPUT_TOPIC = "/dynamic_joint_states";
const OUTPUT_TOPIC = "/inspire_hand/joints";

function readNumeric(values: InterfaceValue["values"], index: number): number {
  if (index < 0) {
    return NaN;
  }
  const v = values[index];
  return typeof v === "number" && Number.isFinite(v) ? v : NaN;
}

function buildEntry(
  name: string,
  iv: InterfaceValue | undefined,
): HandJointEntry {
  if (!iv) {
    return { name, position: NaN, velocity: NaN, force: NaN };
  }
  const ifaceNames = iv.interface_names ?? [];
  const values = iv.values ?? [];
  return {
    name,
    position: readNumeric(values, ifaceNames.indexOf(POSITION)),
    velocity: readNumeric(values, ifaceNames.indexOf(VELOCITY)),
    force: readNumeric(values, ifaceNames.indexOf(FORCE)),
  };
}

export function registerJointStateCombined(
  extensionContext: ExtensionContext,
): void {
  extensionContext.registerMessageConverter({
    type: "topic",
    inputTopics: [INPUT_TOPIC],
    outputTopic: OUTPUT_TOPIC,
    outputSchemaName: HAND_JOINTS_SCHEMA_NAME,
    outputSchemaDescription: HAND_JOINTS_SCHEMA,
    create: () => {
      return (
        msgEvent: Immutable<MessageEvent>,
      ): HandJointsMessage | undefined => {
        const msg = msgEvent.message as Immutable<DynamicJointState>;
        const names = msg.joint_names ?? [];
        const ivs = msg.interface_values ?? [];

        const joints: HandJointEntry[] = JOINT_ORDER.map((jointName) => {
          const idx = names.indexOf(jointName);
          const iv =
            idx >= 0
              ? (ivs[idx] as Immutable<InterfaceValue> | undefined)
              : undefined;
          return buildEntry(jointName, iv as InterfaceValue | undefined);
        });

        return { joints };
      };
    },
  });
}
