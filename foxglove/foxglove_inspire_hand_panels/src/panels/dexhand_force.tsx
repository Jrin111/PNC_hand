// Dexterous Hand Force — compact per-joint live force monitor + threshold control.
//
// Subscribes:
//   /inspire_hand/joints     (renesas.DexterousHandJoints) — joint_state_combined converter
//
// Publishes:
//   /inspire_rh56e2_hand_force_threshold_controller/commands  std_msgs/msg/Float64MultiArray (6)
//
// Live force values are displayed as vertical bars in the range
// FORCE_DISPLAY_MIN..FORCE_DISPLAY_MAX (grams). Zero is rendered as a baseline
// inside each bar; positive forces fill upward, negative forces fill downward.
// The threshold control still spans the full hardware range
// (FORCE_THRESH_MIN..FORCE_THRESH_MAX) — when the configured threshold is
// within the bar's display range, it is also shown as a dashed marker on the
// bar to give a quick visual reference against the live force.

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
  short: string;
}

const JOINTS: readonly JointSpec[] = [
  { key: "thumb_proximal_yaw_joint",   label: "Thumb Yaw",   short: "T-Y" },
  { key: "thumb_proximal_pitch_joint", label: "Thumb Pitch", short: "T-P" },
  { key: "index_proximal_joint",       label: "Index",       short: "Idx" },
  { key: "middle_proximal_joint",      label: "Middle",      short: "Mid" },
  { key: "ring_proximal_joint",        label: "Ring",        short: "Rng" },
  { key: "pinky_proximal_joint",       label: "Pinky",       short: "Pky" },
] as const;

// Bar display range (grams).
const FORCE_DISPLAY_MIN = -100;
const FORCE_DISPLAY_MAX = 1500;
// Hardware threshold range (grams) — kept consistent with the controller config.
const FORCE_THRESH_MIN = 0;
const FORCE_THRESH_MAX = 3000;

const TOPIC_JOINTS = "/inspire_hand/joints";
const TOPIC_FORCE_CMD = "/inspire_rh56e2_hand_force_threshold_controller/commands";

const DT_F64ARR = "std_msgs/msg/Float64MultiArray";

const DEFAULT_THRESHOLDS: [number, number, number, number, number, number] = [300, 300, 300, 300, 300, 300];

// ---- persisted config -------------------------------------------------------

interface PersistedConfig {
  thresholds: number[];
}

function normalizeConfig(input: unknown): PersistedConfig {
  const o = (typeof input === "object" && input !== null ? input : {}) as Record<string, unknown>;
  const arr6 = (v: unknown, fallback: number[]): number[] => {
    if (!Array.isArray(v) || v.length < 6) return fallback.slice();
    return Array.from({ length: 6 }, (_, i) => (typeof v[i] === "number" ? (v[i] as number) : fallback[i]!));
  };
  return {
    thresholds: arr6(o["thresholds"], DEFAULT_THRESHOLDS as unknown as number[]),
  };
}

// ---- helpers ----------------------------------------------------------------

const fmt = (v: number, digits = 0): string =>
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

function jointForceMap(msg: HandJointsMessage | undefined): Map<string, number> {
  const m = new Map<string, number>();
  if (!msg) return m;
  for (const j of msg.joints) m.set(j.name, j.force);
  return m;
}

function buildFloat64MultiArray(data: number[]): Record<string, unknown> {
  return {
    layout: { dim: [{ label: "", size: data.length, stride: data.length }], data_offset: 0 },
    data,
  };
}

// Extract the 6 threshold values out of a Float64MultiArray-shaped message.
// Returns undefined if the payload is not usable (wrong shape, NaN, etc.).
function parseFloat64MultiArray(message: unknown): number[] | undefined {
  const m = message as { data?: ArrayLike<unknown> } | undefined;
  const raw = m?.data;
  if (!raw || typeof raw.length !== "number" || raw.length < JOINTS.length) {
    return undefined;
  }
  const out: number[] = [];
  for (let i = 0; i < JOINTS.length; i++) {
    const v = Number(raw[i]);
    if (!Number.isFinite(v)) return undefined;
    out.push(v);
  }
  return out;
}

// Compare two threshold vectors element-wise within a small tolerance so
// floating-point round-trips through ROS don't trigger a spurious update.
function thresholdsEqual(a: number[], b: number[]): boolean {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) {
    if (Math.abs((a[i] ?? 0) - (b[i] ?? 0)) > 0.5) return false;
  }
  return true;
}

// Map a force value (grams) to a percentage along the bar (0% = bottom = MIN,
// 100% = top = MAX). Out-of-range values are clamped.
function forcePctFromBottom(force: number): number {
  const range = FORCE_DISPLAY_MAX - FORCE_DISPLAY_MIN;
  return clamp(((force - FORCE_DISPLAY_MIN) / range) * 100, 0, 100);
}

const ZERO_PCT = forcePctFromBottom(0);

// ---- the panel --------------------------------------------------------------

function DexhandForce({ context }: { context: PanelExtensionContext }): ReactElement {
  const initial = useMemo(() => normalizeConfig(context.initialState), [context.initialState]);

  const [thresholds, setThresholds] = useState<number[]>(initial.thresholds);

  const jointReceivedAt = useRef<number | undefined>();
  const [jointEvent, setJointEvent] = useState<Immutable<MessageEvent> | undefined>();
  const [renderDone, setRenderDone] = useState<(() => void) | undefined>();
  const [colorScheme, setColorScheme] = useState<"dark" | "light">("dark");
  const [applyFlash, setApplyFlash] = useState<number>(0);

  useEffect(() => {
    context.saveState({ thresholds });
  }, [context, thresholds]);

  useEffect(() => {
    if (!applyFlash) { return; }
    const t = setTimeout(() => { setApplyFlash(0); }, 900);
    return () => { clearTimeout(t); };
  }, [applyFlash]);

  useEffect(() => {
    context.subscribe([{ topic: TOPIC_JOINTS }, { topic: TOPIC_FORCE_CMD }]);
    try { context.advertise?.(TOPIC_FORCE_CMD, DT_F64ARR, ROS2_PUBLISH_OPTIONS); } catch { /* noop */ }
    return () => {
      context.unsubscribeAll();
      try { context.unadvertise?.(TOPIC_FORCE_CMD); } catch { /* noop */ }
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
      const cmd = latestEventOnTopic(renderState.currentFrame, TOPIC_FORCE_CMD);
      if (cmd) {
        const incoming = parseFloat64MultiArray(cmd.message);
        if (incoming) {
          setThresholds((prev) => {
            if (thresholdsEqual(prev, incoming)) return prev;
            // External publisher (e.g. object_force_threshold_setter) changed
            // the thresholds — sync the UI and reuse the "Applied" flash so
            // the operator sees that something landed.
            setApplyFlash((n) => n + 1);
            return incoming;
          });
        }
      }
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

  const publishThresholds = useCallback((vals: number[]) => {
    try { context.publish?.(TOPIC_FORCE_CMD, buildFloat64MultiArray(vals)); } catch { /* noop */ }
  }, [context]);

  const onThresholdChange = useCallback((idx: number, value: number) => {
    setThresholds((prev) => {
      const next = prev.slice();
      next[idx] = clamp(value, FORCE_THRESH_MIN, FORCE_THRESH_MAX);
      return next;
    });
  }, []);

  // Drag the dashed threshold line (or anywhere on the bar track) to set
  // the per-joint threshold. The bar's visible range is 0..FORCE_DISPLAY_MAX,
  // so dragging is clamped to that subrange. The slider below still spans
  // the full 0..FORCE_THRESH_MAX hardware range.
  const onBarPointer = useCallback((idx: number, ev: React.PointerEvent<HTMLDivElement>) => {
    // Only react to primary button (or touch/pen).
    if (ev.button !== undefined && ev.button !== 0) return;
    const track = ev.currentTarget;
    track.setPointerCapture(ev.pointerId);

    const update = (clientY: number) => {
      const rect = track.getBoundingClientRect();
      if (rect.height <= 0) return;
      const yFromBottom = clamp(rect.bottom - clientY, 0, rect.height);
      const pct = (yFromBottom / rect.height) * 100;
      // pct is 0% at bottom (FORCE_DISPLAY_MIN) → 100% at top (FORCE_DISPLAY_MAX).
      const grams = FORCE_DISPLAY_MIN + (pct / 100) * (FORCE_DISPLAY_MAX - FORCE_DISPLAY_MIN);
      // Threshold has hardware floor at 0 g.
      onThresholdChange(idx, Math.max(0, Math.round(grams / 5) * 5));
    };

    update(ev.clientY);

    const onMove = (e: PointerEvent) => update(e.clientY);
    const onUp = (e: PointerEvent) => {
      try { track.releasePointerCapture(e.pointerId); } catch { /* noop */ }
      track.removeEventListener("pointermove", onMove);
      track.removeEventListener("pointerup", onUp);
      track.removeEventListener("pointercancel", onUp);
    };
    track.addEventListener("pointermove", onMove);
    track.addEventListener("pointerup", onUp);
    track.addEventListener("pointercancel", onUp);
  }, [onThresholdChange]);

  const onBarKeyDown = useCallback((idx: number, ev: React.KeyboardEvent<HTMLDivElement>) => {
    let delta = 0;
    let absolute: number | undefined;
    switch (ev.key) {
      case "ArrowUp":
      case "ArrowRight":   delta = ev.shiftKey ? 50 : 5; break;
      case "ArrowDown":
      case "ArrowLeft":    delta = ev.shiftKey ? -50 : -5; break;
      case "PageUp":       delta = 50; break;
      case "PageDown":     delta = -50; break;
      case "Home":         absolute = FORCE_THRESH_MIN; break;
      case "End":          absolute = FORCE_THRESH_MAX; break;
      default: return;
    }
    ev.preventDefault();
    setThresholds((prev) => {
      const cur = prev[idx] ?? 0;
      const target = absolute !== undefined ? absolute : cur + delta;
      const next = prev.slice();
      next[idx] = clamp(Math.round(target / 5) * 5, FORCE_THRESH_MIN, FORCE_THRESH_MAX);
      return next;
    });
  }, []);

  const onMasterThreshold = useCallback((value: number) => {
    const v = clamp(value, FORCE_THRESH_MIN, FORCE_THRESH_MAX);
    const next = JOINTS.map(() => v);
    setThresholds(next);
  }, []);

  const jointsMsg = jointEvent?.message as HandJointsMessage | undefined;
  const forceMap = useMemo(() => jointForceMap(jointsMsg), [jointsMsg]);

  const liveJointStatus = jointEvent ? "live" : "waiting";

  return (
    <div className="ihc-root" data-theme={colorScheme}>
      <style>{PANEL_CSS}</style>

      <header className="ihc-header">
        <div>
          <div className="ihc-kicker">Dexterous Hand</div>
          <h2 className="ihc-title">Force Monitor</h2>
          <div className="ihc-subtitle">force feedback • per-joint thresholds · unavailable feedback is Unknown</div>
        </div>
        <div className="ihc-stats">
          <div className="ihc-stat">
            <div className="ihc-stat-label">Joints</div>
            <div className={`ihc-stat-value ${liveJointStatus === "live" ? "live" : ""}`}>{liveJointStatus}</div>
          </div>
        </div>
      </header>

      <section className="ihc-section force">
        <div className="ihc-section-head">
          <span className="ihc-section-title">Live Force</span>
          <span className="ihc-section-hint">{FORCE_DISPLAY_MIN}..{FORCE_DISPLAY_MAX} g</span>
          <div className="ihc-actions">
            <button
              className={`ihc-btn ihf-apply-btn ${applyFlash ? "applied" : ""}`}
              onClick={() => {
                publishThresholds(thresholds);
                setApplyFlash((n) => n + 1);
              }}
            >
              {applyFlash ? "Applied ✓" : "Apply Thresholds"}
            </button>
          </div>
        </div>

        <div className="ihf-bars">
          <div className="ihf-bar-col ihf-axis-col" aria-hidden>
            <div className="ihf-bar-value-top">
              <span className="ihf-bar-value">0</span>
              <span className="ihf-bar-unit">g</span>
            </div>
            <div className="ihf-bar-track ihf-axis-track">
              <span className="ihf-bar-axis-tick" style={{ bottom: "100%" }}>{FORCE_DISPLAY_MAX}</span>
              <span className="ihf-bar-axis-tick" style={{ bottom: `${ZERO_PCT}%` }}>0</span>
              <span className="ihf-bar-axis-tick" style={{ bottom: "0%" }}>{FORCE_DISPLAY_MIN}</span>
            </div>
            <div className="ihf-bar-name">&nbsp;</div>
            <div className="ihf-bar-thresh-readout">&nbsp;</div>
          </div>

          {JOINTS.map((spec, idx) => {
            const force = forceMap.get(spec.key) ?? NaN;
            const forceKnown = Number.isFinite(force);
            const threshold = thresholds[idx] ?? 0;

            const positive = force >= 0;
            const fillPct = !forceKnown ? 0 : positive
              ? clamp((force / FORCE_DISPLAY_MAX) * (100 - ZERO_PCT), 0, 100 - ZERO_PCT)
              : clamp((-force / -FORCE_DISPLAY_MIN) * ZERO_PCT, 0, ZERO_PCT);

            const thresholdInRange = threshold <= FORCE_DISPLAY_MAX;
            const threshPct = forcePctFromBottom(threshold);

            const overThreshold = Math.abs(force) >= threshold && threshold > 0;

            return (
              <div className={`ihf-bar-col ${overThreshold ? "alert" : ""}`} key={spec.key}>
                <div className="ihf-bar-value-top">
                  <span className={`ihf-bar-value ${overThreshold ? "alert" : ""}`}>{fmt(force, 0)}</span>
                  <span className="ihf-bar-unit">g</span>
                </div>

                <div
                  className="ihf-bar-track draggable"
                  title={`${spec.label}: ${fmt(force, 0)} g (thr ${fmt(threshold, 0)} g) — drag or use ↑/↓ to set threshold`}
                  tabIndex={0}
                  role="slider"
                  aria-label={`${spec.label} force threshold`}
                  aria-valuemin={FORCE_THRESH_MIN}
                  aria-valuemax={FORCE_THRESH_MAX}
                  aria-valuenow={threshold}
                  onPointerDown={(e) => onBarPointer(idx, e)}
                  onKeyDown={(e) => onBarKeyDown(idx, e)}
                >
                  <div className="ihf-bar-zero" style={{ bottom: `${ZERO_PCT}%` }} />
                  <div
                    className={`ihf-bar-fill ${positive ? "pos" : "neg"} ${overThreshold ? "alert" : ""}`}
                    style={
                      positive
                        ? { bottom: `${ZERO_PCT}%`, height: `${fillPct}%` }
                        : { top: `${100 - ZERO_PCT}%`, height: `${fillPct}%` }
                    }
                  />
                  {thresholdInRange && threshold > 0 && (
                    <div className="ihf-bar-thresh" style={{ bottom: `${threshPct}%` }} aria-hidden />
                  )}
                  {!thresholdInRange && (
                    <div className="ihf-bar-thresh-off" aria-hidden title={`threshold ${fmt(threshold, 0)} g (above bar range)`}>▲</div>
                  )}
                </div>

                <div className="ihf-bar-name">{spec.label}</div>
                <div className="ihf-bar-thresh-readout">{fmt(threshold, 0)} g</div>
              </div>
            );
          })}
        </div>

        <div className="ihf-master">
          <span className="ihf-master-label">all thresholds</span>
          <input
            type="range"
            min={FORCE_THRESH_MIN}
            max={FORCE_THRESH_MAX}
            step={10}
            onChange={(e) => onMasterThreshold(parseFloat(e.target.value))}
          />
          <span className="ihf-master-hint">drag to set every joint at once</span>
        </div>
      </section>
    </div>
  );
}

export function initDexhandForce(context: PanelExtensionContext): () => void {
  context.setDefaultPanelTitle?.("Dexterous Hand Force");
  const root = createRoot(context.panelElement);
  root.render(<DexhandForce context={context} />);
  return () => { root.unmount(); };
}
