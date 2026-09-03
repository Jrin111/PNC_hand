// Dexterous Hand Gripper — compact gripper command publisher.
//
// Publishes:
//   /gripper_command    control_msgs/msg/GripperCommand
//
// Subscribes:
//   /gripper_max_width  std_msgs/msg/Float64
//
// The range defaults to 0..0.06 m and follows the runtime max width published
// by hand_gripper_action_adapter when available.

import {
  Immutable,
  MessageEvent,
  PanelExtensionContext,
} from "@foxglove/extension";
import { ReactElement, useCallback, useEffect, useLayoutEffect, useMemo, useState } from "react";
import { createRoot } from "react-dom/client";

import { ROS2_PUBLISH_OPTIONS } from "../schemas/ros2_publish";

import { PANEL_CSS } from "./theme.css";

const GRIPPER_MIN = 0.0;
const DEFAULT_GRIPPER_MAX = 0.06; // metres

const TOPIC_GRIPPER = "/gripper_command";
const TOPIC_GRIPPER_MAX_WIDTH = "/gripper_max_width";
const DT_GRIPPER = "control_msgs/msg/GripperCommand";

interface PersistedConfig {
  gripperPosition: number;
  liveMode: boolean;
}

function normalizeConfig(input: unknown): PersistedConfig {
  const o = (typeof input === "object" && input !== null ? input : {}) as Record<string, unknown>;
  return {
    gripperPosition: typeof o["gripperPosition"] === "number" ? (o["gripperPosition"] as number) : 0.02,
    liveMode: o["liveMode"] !== false,
  };
}

const clamp = (v: number, lo: number, hi: number): number => Math.min(hi, Math.max(lo, v));

function latestEventOnTopic(
  frame: Immutable<MessageEvent[]> | undefined,
  topic: string,
): Immutable<MessageEvent> | undefined {
  let latest: Immutable<MessageEvent> | undefined;
  for (const ev of frame ?? []) if (ev.topic === topic) latest = ev;
  return latest;
}

function parseFloat64(msg: unknown): number | undefined {
  const data = (msg as { data?: unknown } | undefined)?.data;
  return typeof data === "number" && Number.isFinite(data) && data > 0 ? data : undefined;
}

function DexhandGripper({ context }: { context: PanelExtensionContext }): ReactElement {
  const initial = useMemo(() => normalizeConfig(context.initialState), [context.initialState]);

  const [gripperPosition, setGripperPosition] = useState<number>(initial.gripperPosition);
  const [gripperMaxWidth, setGripperMaxWidth] = useState<number>(DEFAULT_GRIPPER_MAX);
  const [liveMode, setLiveMode] = useState<boolean>(initial.liveMode);
  const [renderDone, setRenderDone] = useState<(() => void) | undefined>();
  const [colorScheme, setColorScheme] = useState<"dark" | "light">("dark");

  useEffect(() => {
    context.saveState({ gripperPosition, liveMode });
  }, [context, gripperPosition, liveMode]);

  useEffect(() => {
    context.subscribe([{ topic: TOPIC_GRIPPER_MAX_WIDTH }]);
    try { context.advertise?.(TOPIC_GRIPPER, DT_GRIPPER, ROS2_PUBLISH_OPTIONS); } catch { /* noop */ }
    return () => {
      context.unsubscribeAll();
      try { context.unadvertise?.(TOPIC_GRIPPER); } catch { /* noop */ }
    };
  }, [context]);

  useLayoutEffect(() => {
    context.onRender = (renderState, done) => {
      const maxWidthEvent = latestEventOnTopic(renderState.currentFrame, TOPIC_GRIPPER_MAX_WIDTH);
      const maxWidth = maxWidthEvent ? parseFloat64(maxWidthEvent.message) : undefined;
      if (maxWidth !== undefined) setGripperMaxWidth(maxWidth);
      if (renderState.colorScheme) setColorScheme(renderState.colorScheme);
      setRenderDone(() => done);
    };
    context.watch("currentFrame");
    context.watch("topics");
    context.watch("colorScheme");
    return () => { context.onRender = undefined; };
  }, [context]);

  useEffect(() => { renderDone?.(); }, [renderDone]);

  useEffect(() => {
    setGripperPosition((prev) => clamp(prev, GRIPPER_MIN, gripperMaxWidth));
  }, [gripperMaxWidth]);

  const publishGripper = useCallback((pos: number) => {
    const safePos = clamp(pos, GRIPPER_MIN, gripperMaxWidth);
    try { context.publish?.(TOPIC_GRIPPER, { position: safePos, max_effort: 0 }); } catch { /* noop */ }
  }, [context, gripperMaxWidth]);

  const onGripperChange = useCallback((value: number) => {
    const v = clamp(value, GRIPPER_MIN, gripperMaxWidth);
    setGripperPosition(v);
    if (liveMode) publishGripper(v);
  }, [gripperMaxWidth, liveMode, publishGripper]);

  return (
    <div className="ihc-root" data-theme={colorScheme}>
      <style>{PANEL_CSS}</style>

      <header className="ihc-header">
        <div>
          <div className="ihc-kicker">Dexterous Hand</div>
          <h2 className="ihc-title">Gripper</h2>
          <div className="ihc-subtitle">/gripper_command</div>
        </div>
        <div className="ihc-stats">
          <button
            className={`ihc-live-toggle ${liveMode ? "on" : ""}`}
            onClick={() => setLiveMode((v) => !v)}
            title="When on, slider changes publish immediately."
          >
            <span className="ihc-live-dot" />
            Live {liveMode ? "ON" : "OFF"}
          </button>
        </div>
      </header>

      <section className="ihc-section gripper tall">
        <div className="ihc-gripper-row single tall">
          <label className="ihc-gripper-field wide tall">
            <span>width</span>
            <input
              type="range"
              min={GRIPPER_MIN}
              max={gripperMaxWidth}
              step={0.001}
              value={gripperPosition}
              onChange={(e) => onGripperChange(parseFloat(e.target.value))}
            />
            <span className="ihc-gripper-readout">{(gripperPosition * 100).toFixed(1)} cm</span>
          </label>
          <button
            className="ihc-btn primary"
            onClick={() => publishGripper(gripperPosition)}
            disabled={liveMode}
            title={liveMode ? "Live mode: gripper is published on slider change" : "Send gripper command"}
          >
            Send
          </button>
        </div>
      </section>
    </div>
  );
}

export function initDexhandGripper(context: PanelExtensionContext): () => void {
  context.setDefaultPanelTitle?.("Dexterous Hand Gripper");
  const root = createRoot(context.panelElement);
  root.render(<DexhandGripper context={context} />);
  return () => { root.unmount(); };
}
