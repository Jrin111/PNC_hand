// Dexterous Hand Console — combined status + control panel for joints + gripper.
//
// Subscribes:
//   /inspire_hand/joints     (renesas.DexterousHandJoints) — joint_state_combined converter
//
// Publishes:
//   /inspire_rh56e2_hand_joint_position_controller/commands   std_msgs/msg/Float64MultiArray (6)
//
// Joint limits are taken from the hand description URDF. Force thresholds
// and the live force readout live in the separate "Dexterous Hand Force"
// panel; the gripper command lives in the separate "Dexterous Hand Gripper"
// panel.

import {
  Immutable,
  MessageEvent,
  PanelExtensionContext,
} from "@foxglove/extension";
import { ReactElement, useCallback, useEffect, useLayoutEffect, useMemo, useRef, useState } from "react";
import { createRoot } from "react-dom/client";

import { ROS2_PUBLISH_OPTIONS } from "../schemas/ros2_publish";

import { PANEL_CSS } from "./theme.css";
import type { HandJointsMessage } from "../schemas/messages";

// ---- domain constants -------------------------------------------------------

interface JointSpec {
  key: string;
  label: string;
  lower: number;
  upper: number;
}

// Lower/upper from URDF <limit> tags (right-hand macro; left mirrors them).
const JOINTS: readonly JointSpec[] = [
  { key: "thumb_proximal_yaw_joint",   label: "Thumb Yaw",   lower: 0, upper: 1.658 },
  { key: "thumb_proximal_pitch_joint", label: "Thumb Pitch", lower: 0, upper: 0.62  },
  { key: "index_proximal_joint",       label: "Index",       lower: 0, upper: 1.4381 },
  { key: "middle_proximal_joint",      label: "Middle",      lower: 0, upper: 1.4381 },
  { key: "ring_proximal_joint",        label: "Ring",        lower: 0, upper: 1.4381 },
  { key: "pinky_proximal_joint",       label: "Pinky",       lower: 0, upper: 1.4381 },
] as const;

const TOPIC_JOINTS = "/inspire_hand/joints";
const TOPIC_POS_CMD = "/inspire_rh56e2_hand_joint_position_controller/commands";

const DT_F64ARR = "std_msgs/msg/Float64MultiArray";

interface Preset {
  name: string;
  values: [number, number, number, number, number, number];
}

const PRESETS: Preset[] = [
  { name: "Open",  values: [0.0, 0.0, 0.0, 0.0, 0.0, 0.0] },
  { name: "Fist",  values: [0.4, 0.5, 1.4, 1.4, 1.4, 1.4] },
  { name: "One",   values: [0.4, 0.5, 0.0, 1.4, 1.4, 1.4] },
  { name: "Two",   values: [0.4, 0.5, 0.0, 0.0, 1.4, 1.4] },
  { name: "Three", values: [0.4, 0.5, 0.0, 0.0, 0.0, 1.4] },
  { name: "Four",  values: [0.4, 0.5, 0.0, 0.0, 0.0, 0.0] },
];

const DEFAULT_POSITIONS: [number, number, number, number, number, number] = [1.6, 0.6, 0.0, 0.0, 1.4, 1.4];

// ---- persisted config -------------------------------------------------------

interface PersistedConfig {
  positions: number[];
  liveMode: boolean;
}

function normalizeConfig(input: unknown): PersistedConfig {
  const o = (typeof input === "object" && input !== null ? input : {}) as Record<string, unknown>;
  const arr6 = (v: unknown, fallback: number[]): number[] => {
    if (!Array.isArray(v) || v.length < 6) return fallback.slice();
    return Array.from({ length: 6 }, (_, i) => (typeof v[i] === "number" ? (v[i] as number) : fallback[i]!));
  };
  return {
    positions: arr6(o["positions"], DEFAULT_POSITIONS as unknown as number[]),
    liveMode: o["liveMode"] !== false,
  };
}

// ---- helpers ----------------------------------------------------------------

const fmt = (v: number, digits = 2): string =>
  Number.isFinite(v) ? v.toFixed(digits) : "Unknown";

const clamp = (v: number, lo: number, hi: number): number => Math.min(hi, Math.max(lo, v));

function latestEventOnTopic(
  frame: Immutable<MessageEvent[]> | undefined,
  topic: string,
): Immutable<MessageEvent> | undefined {
  let latest: Immutable<MessageEvent> | undefined;
  for (const ev of frame ?? []) if (ev.topic === topic) latest = ev;
  return latest;
}

function jointMapFromMessage(msg: HandJointsMessage | undefined): Map<string, { position: number; velocity: number }> {
  const m = new Map<string, { position: number; velocity: number }>();
  if (!msg) return m;
  for (const j of msg.joints) {
    m.set(j.name, { position: j.position, velocity: j.velocity });
  }
  return m;
}

function buildFloat64MultiArray(data: number[]): Record<string, unknown> {
  return {
    layout: { dim: [{ label: "", size: data.length, stride: data.length }], data_offset: 0 },
    data,
  };
}

// ---- the panel --------------------------------------------------------------

function DexhandConsole({ context }: { context: PanelExtensionContext }): ReactElement {
  const initial = useMemo(() => normalizeConfig(context.initialState), [context.initialState]);

  const [positions, setPositions] = useState<number[]>(initial.positions);
  const [liveMode, setLiveMode] = useState<boolean>(initial.liveMode);

  const jointReceivedAt = useRef<number | undefined>();
  const [jointEvent, setJointEvent] = useState<Immutable<MessageEvent> | undefined>();
  const [renderDone, setRenderDone] = useState<(() => void) | undefined>();
  const [colorScheme, setColorScheme] = useState<"dark" | "light">("dark");
  const [applyFlash, setApplyFlash] = useState<number>(0);

  useEffect(() => {
    context.saveState({ positions, liveMode });
  }, [context, positions, liveMode]);

  useEffect(() => {
    if (!applyFlash) { return; }
    const t = setTimeout(() => { setApplyFlash(0); }, 900);
    return () => { clearTimeout(t); };
  }, [applyFlash]);

  useEffect(() => {
    context.subscribe([{ topic: TOPIC_JOINTS }]);
    try { context.advertise?.(TOPIC_POS_CMD, DT_F64ARR, ROS2_PUBLISH_OPTIONS); } catch { /* noop */ }
    return () => {
      context.unsubscribeAll();
      try { context.unadvertise?.(TOPIC_POS_CMD); } catch { /* noop */ }
    };
  }, [context]);

  useEffect(() => {
    const timer = window.setInterval(() => {
      if (jointReceivedAt.current !== undefined && Date.now() - jointReceivedAt.current >= 2000) {
        jointReceivedAt.current = undefined;
        setJointEvent(undefined);
      }
    }, 250);
    return () => window.clearInterval(timer);
  }, []);

  useLayoutEffect(() => {
    context.onRender = (renderState, done) => {
      if (renderState.didSeek) { jointReceivedAt.current = undefined; setJointEvent(undefined); }
      const j = latestEventOnTopic(renderState.currentFrame, TOPIC_JOINTS);
      if (j) { jointReceivedAt.current = Date.now(); setJointEvent(j); }
      if (renderState.colorScheme) setColorScheme(renderState.colorScheme);
      setRenderDone(() => done);
    };
    context.watch("currentFrame");
    context.watch("didSeek");
    context.watch("topics");
    context.watch("colorScheme");
    return () => { context.onRender = undefined; };
  }, [context]);

  useEffect(() => { renderDone?.(); }, [renderDone]);

  const publishPositions = useCallback((vals: number[]) => {
    try { context.publish?.(TOPIC_POS_CMD, buildFloat64MultiArray(vals)); } catch { /* noop */ }
  }, [context]);

  const onPositionChange = useCallback((idx: number, value: number) => {
    setPositions((prev) => {
      const next = prev.slice();
      const spec = JOINTS[idx]!;
      next[idx] = clamp(value, spec.lower, spec.upper);
      if (liveMode) publishPositions(next);
      return next;
    });
  }, [liveMode, publishPositions]);

  const onMasterFraction = useCallback((fraction: number) => {
    setPositions(() => {
      const next = JOINTS.map((s) => s.lower + clamp(fraction, 0, 1) * (s.upper - s.lower));
      if (liveMode) publishPositions(next);
      return next;
    });
  }, [liveMode, publishPositions]);

  const onPresetClick = useCallback((preset: Preset) => {
    const next = preset.values.map((v, i) => {
      const s = JOINTS[i]!;
      return clamp(v, s.lower, s.upper);
    });
    setPositions(next);
    publishPositions(next);
  }, [publishPositions]);

  const jointsMsg = jointEvent?.message as HandJointsMessage | undefined;
  const jointMap = useMemo(() => jointMapFromMessage(jointsMsg), [jointsMsg]);

  const liveJointStatus = jointEvent ? "live" : "waiting";

  return (
    <div className="ihc-root console" data-theme={colorScheme}>
      <style>{PANEL_CSS}</style>

      <header className="ihc-header">
        <div>
          <div className="ihc-kicker">Dexterous Hand</div>
          <h2 className="ihc-title">Hand Console</h2>
          <div className="ihc-subtitle">joints • presets</div>
        </div>
        <div className="ihc-stats">
          <div className="ihc-stat">
            <div className="ihc-stat-label">Joints</div>
            <div className={`ihc-stat-value ${liveJointStatus === "live" ? "live" : ""}`}>{liveJointStatus}</div>
          </div>
          <button
            className={`ihc-live-toggle ${liveMode ? "on" : ""}`}
            onClick={() => setLiveMode((v) => !v)}
            title="When on, slider changes publish immediately. Otherwise, click Apply to publish."
          >
            <span className="ihc-live-dot" />
            Live {liveMode ? "ON" : "OFF"}
          </button>
        </div>
      </header>

      <section className="ihc-section">
        <div className="ihc-section-head">
          <span className="ihc-section-title">Joints</span>
          <span className="ihc-section-hint">drag to set target • read-back below</span>
          <div className="ihc-actions">
            <label className="ihc-master">
              <span>master</span>
              <input
                type="range"
                min={0}
                max={1}
                step={0.01}
                onChange={(e) => onMasterFraction(parseFloat(e.target.value))}
              />
            </label>
            <button
              className={`ihc-btn primary ihf-apply-btn ${applyFlash ? "applied" : ""}`}
              onClick={() => { publishPositions(positions); setApplyFlash((n) => n + 1); }}
            >
              {applyFlash ? "Applied ✓" : "Apply Positions"}
            </button>
          </div>
        </div>

        <div className="ihc-grid joints">
          {JOINTS.map((spec, idx) => {
            const live = jointMap.get(spec.key);
            const target = positions[idx] ?? spec.lower;
            const range = spec.upper - spec.lower || 1;
            const livePos = live?.position ?? NaN;
            const livePct = Number.isFinite(livePos) ? clamp(((livePos - spec.lower) / range) * 100, 0, 100) : 0;
            return (
              <div className="ihc-card joint" key={spec.key}>
                <div className="ihc-card-head">
                  <span className="ihc-card-title">{spec.label}</span>
                  <span className="ihc-card-tag">[{fmt(spec.lower, 2)} … {fmt(spec.upper, 2)}]</span>
                </div>

                {/* Single styled slider: thumb = target; the track fill colour
                    indicates the live joint position. */}
                <input
                  className="ihc-slider position"
                  type="range"
                  min={spec.lower}
                  max={spec.upper}
                  step={0.01}
                  value={target}
                  onChange={(e) => onPositionChange(idx, parseFloat(e.target.value))}
                  style={{ ["--live" as never]: `${livePct}%` }}
                />

                <div className="ihc-readout-row">
                  <span className="ihc-readout">
                    <span className="ihc-readout-label">target</span>
                    <span className="ihc-readout-value primary">{fmt(target, 2)}</span>
                  </span>
                  <span className="ihc-readout">
                    <span className="ihc-readout-label">live</span>
                    <span className="ihc-readout-value">{fmt(livePos, 2)}</span>
                  </span>
                </div>
              </div>
            );
          })}
        </div>

        <div className="ihc-presets">
          {PRESETS.map((preset) => (
            <button key={preset.name} className="ihc-preset" onClick={() => onPresetClick(preset)}>
              {preset.name}
            </button>
          ))}
        </div>
      </section>
    </div>
  );
}

export function initDexhandConsole(context: PanelExtensionContext): () => void {
  context.setDefaultPanelTitle?.("Dexterous Hand Console");
  const root = createRoot(context.panelElement);
  root.render(<DexhandConsole context={context} />);
  return () => { root.unmount(); };
}
