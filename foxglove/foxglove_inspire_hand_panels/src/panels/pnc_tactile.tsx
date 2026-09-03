import type { PanelExtensionContext } from "@foxglove/extension";
import { useCallback, useEffect, useLayoutEffect, useRef, useState, type ReactElement } from "react";
import { createRoot } from "react-dom/client";
import { CHANNEL_COUNT, CHANNEL_NAMES, DEMO_ENABLED_TOPIC, DEMO_VALUES_TOPIC, NAMES_TOPIC,
  VALUES_TOPIC, STALE_AFTER_MS, SimulationLease, TactileDecoder, pressureColor, PRESSURE_GRADIENT,
  simulationArray, unknownFrame, type TactileFrame } from "../data/pnc_tactile";
import { ROS2_PUBLISH_OPTIONS } from "../schemas/ros2_publish";

const TOPICS = [NAMES_TOPIC, VALUES_TOPIC, DEMO_ENABLED_TOPIC].map((topic) => ({ topic }));
const CSS = `
 .pnc {font:13px system-ui,sans-serif;height:100%;overflow:auto;padding:18px;box-sizing:border-box;background:#111820;color:#e1e9f1}
 .pnc[data-theme=light]{background:#f6f8fb;color:#152433}
 .pnc header,.pnc .toolbar,.pnc .details{display:flex;align-items:center;gap:14px;flex-wrap:wrap;justify-content:space-between}
 .pnc h2{font-size:21px;margin:3px 0 6px}.pnc p{line-height:1.5;margin:8px 0}.pnc small{opacity:.7}
 .pnc .badge{font-size:11px;text-transform:uppercase;letter-spacing:.07em;padding:6px 9px;border:1px solid #617384;border-radius:20px}
 .pnc .live{color:#54d4b0;border-color:#54d4b0}.pnc .simulation{color:#f1bc59;border-color:#f1bc59}
 .pnc button,.pnc select{font:inherit;border:1px solid #627184;background:transparent;color:inherit;border-radius:6px;padding:8px 10px;cursor:pointer}
 .pnc button:disabled{opacity:.4;cursor:not-allowed}.pnc button:focus-visible{outline:2px solid #68c9ff;outline-offset:2px}
 .pnc section{border-top:1px solid #61738455;margin-top:18px;padding-top:15px}.pnc h3{font-size:14px;margin:0 0 12px}
 .pnc .bank{display:grid;grid-template-columns:42px repeat(6,minmax(49px,1fr));align-items:center;gap:6px;margin:6px 0}
 .pnc .channel{text-align:left;padding:7px 8px;border:1px solid #61738466;border-left:4px solid var(--heat);min-height:52px}
 .pnc .channel[aria-pressed=true]{outline:2px solid #80c7ee;border-color:transparent}.pnc .channel strong{display:block;font-variant-numeric:tabular-nums;font-size:13px;margin-top:3px}
 .pnc .scale{height:7px;flex:1;min-width:120px;max-width:230px;background:${PRESSURE_GRADIENT};border-radius:4px}
 .pnc .details{justify-content:flex-start;padding:12px;background:#61738418;border-radius:6px;margin:12px 0;font-variant-numeric:tabular-nums}
 .pnc .details span{min-width:100px}.pnc .unknown{color:#9ca9b8}.pnc input[type=range]{width:min(100%,320px)}
 .pnc .error{color:#ffaf92}.pnc label{display:flex;align-items:center;gap:8px;margin:10px 0}
 @media(max-width:440px){.pnc{padding:10px}.pnc .bank{grid-template-columns:32px repeat(6,minmax(34px,1fr));gap:3px}.pnc .channel{padding:6px 3px}.pnc .channel strong{font-size:11px}}
`;
const fmt = (value: number): string => Number.isFinite(value) ? value.toFixed(3) : "Unknown";

function PncTactile({ context }: { context: PanelExtensionContext }): ReactElement {
  const decoder = useRef(new TactileDecoder());
  const lease = useRef(new SimulationLease());
  const valuesAt = useRef<number | undefined>();
  const lastTime = useRef<number | undefined>();
  const pausedRef = useRef(false);
  const advertised = useRef(false);
  const injectionRef = useRef(false);
  const [frame, setFrame] = useState<TactileFrame>(unknownFrame);
  const [status, setStatus] = useState("Waiting");
  const [theme, setTheme] = useState<"dark" | "light">("dark");
  const [selected, setSelected] = useState(0);
  const [paused, setPaused] = useState(false);
  const [now, setNow] = useState(Date.now());
  const [injection, setInjection] = useState(false);
  const [draft, setDraft] = useState<number[]>(() => Array(CHANNEL_COUNT).fill(0) as number[]);
  const [error, setError] = useState("");
  const [renderDone, setRenderDone] = useState<(() => void) | undefined>();

  const reset = useCallback((reason: string) => {
    decoder.current.reset(); lease.current.reset(); valuesAt.current = undefined;
    injectionRef.current = false; setInjection(false);
    setDraft(Array(CHANNEL_COUNT).fill(0) as number[]);
    setFrame(unknownFrame()); setStatus(reason);
  }, []);

  const resubscribe = useCallback(() => {
    context.unsubscribeAll();
    context.subscribe(TOPICS);
  }, [context]);

  useEffect(() => {
    context.subscribe(TOPICS);
    const timer = window.setInterval(() => {
      const time = Date.now();
      setNow(time);
      if (!lease.current.available(time)) {
        injectionRef.current = false; setInjection(false);
      }
      if (valuesAt.current !== undefined && time - valuesAt.current >= STALE_AFTER_MS) {
        reset("Stale — no new values");
        // Clear cached names as well, then request transient-local Keys again.
        resubscribe();
      }
    }, 250);
    return () => { window.clearInterval(timer); context.unsubscribeAll(); };
  }, [context, reset, resubscribe]);

  useLayoutEffect(() => {
    context.onRender = (state, done) => {
      const time = Date.now();
      const playback = state.currentTime;
      const seconds = playback ? playback.sec + playback.nsec / 1e9 : undefined;
      const reversed = seconds !== undefined && lastTime.current !== undefined && seconds < lastTime.current;
      if (seconds !== undefined) lastTime.current = seconds;
      if (state.didSeek || reversed) reset("Waiting after seek");
      if (state.topics && !state.topics.some((topic) => topic.name === VALUES_TOPIC)) {
        reset("Disconnected / topic unavailable");
      }
      if (!pausedRef.current) {
        for (const event of state.currentFrame ?? []) {
          if (event.topic === NAMES_TOPIC) {
            // Names are replayed when a source reconnects/reactivates. Require a
            // new explicit enable even if that reconnect was shorter than the TTL.
            reset("Waiting for values");
            decoder.current.setNames(event.message);
            setFrame(unknownFrame());
          } else if (event.topic === VALUES_TOPIC) {
            const next = decoder.current.decode(event.message);
            valuesAt.current = time;
            setFrame(next); setStatus("Live stream");
          } else if (event.topic === DEMO_ENABLED_TOPIC) {
            lease.current.observe(event.message, time);
            if (!lease.current.available(time)) { injectionRef.current = false; setInjection(false); }
          }
        }
      }
      if (state.colorScheme) setTheme(state.colorScheme);
      setNow(time); setRenderDone(() => done);
    };
    context.watch("currentFrame"); context.watch("topics"); context.watch("currentTime");
    context.watch("didSeek"); context.watch("colorScheme");
    return () => { context.onRender = undefined; };
  }, [context, reset]);
  useEffect(() => { renderDone?.(); }, [renderDone]);

  const simulation = lease.current.available(now);
  const streamLive = valuesAt.current !== undefined && now - valuesAt.current < STALE_AFTER_MS && !paused;
  const canInject = simulation && streamLive && !!context.publish && !!context.advertise;
  useEffect(() => {
    if (!injection || !canInject) return undefined;
    try {
      context.advertise?.(DEMO_VALUES_TOPIC, "std_msgs/msg/Float64MultiArray", ROS2_PUBLISH_OPTIONS);
      advertised.current = true; setError("");
    } catch (cause) {
      advertised.current = false; injectionRef.current = false; setInjection(false);
      setError(`Cannot advertise simulation input: ${String(cause)}`);
    }
    return () => {
      advertised.current = false;
      try { context.unadvertise?.(DEMO_VALUES_TOPIC); } catch { /* connection may already be gone */ }
    };
  }, [context, injection, canInject]);

  const publish = (values: number[]): void => {
    const time = Date.now();
    // Re-check the lease at the command boundary; a stale React render cannot authorize sending.
    if (!injectionRef.current || !advertised.current || !lease.current.available(time) ||
        valuesAt.current === undefined || time - valuesAt.current >= STALE_AFTER_MS || pausedRef.current) {
      setError("Simulation input is disabled. Wait for the live simulator heartbeat."); return;
    }
    try {
      if (!context.publish) throw new Error("This connection does not support publishing.");
      context.publish(DEMO_VALUES_TOPIC, simulationArray(values));
      setDraft(values); setError("");
    } catch (cause) { setError(`Simulation publish failed: ${String(cause)}`); }
  };

  const channel = frame.channels[selected]!;
  const validCount = frame.channels.filter((entry) => entry.value_known).length;
  return <div className="pnc" data-theme={theme}>
    <style>{CSS}</style>
    <header>
      <div><small>PNC · 9 devices × 6 electrical channels</small><h2>Tactile diagnostics</h2></div>
      <span className={`badge ${streamLive ? "live" : ""}`}>{paused ? "Paused" : status}</span>
    </header>
    <p><span className={`badge ${simulation ? "simulation" : ""}`}>{simulation ? "Simulation" : "Simulation not confirmed"}</span> {validCount}/54 pressure values known</p>
    <p><small>Value = EMA(I) − tare · relative units, not Newtons. Live describes message arrival, not chip health.</small></p>
    <div className="toolbar"><small>0</small><span className="scale" /><small>1 relative unit · gray = unknown</small>
      <button onClick={() => {
        const next = !pausedRef.current; pausedRef.current = next; setPaused(next);
        reset(next ? "Paused" : "Waiting");
        if (!next) resubscribe();
      }}>{paused ? "Resume" : "Pause / clear"}</button>
    </div>
    {!frame.names_received && <p className="unknown">Waiting for an unambiguous names map and matching values.</p>}
    <section aria-label="54 electrical channels">
      {Array.from({ length: 9 }, (_, chip) => <div className="bank" key={chip}>
        <small>RAA{chip}</small>
        {frame.channels.slice(chip * 6, chip * 6 + 6).map((entry, offset) => {
          const index = chip * 6 + offset;
          return <button key={entry.name} className="channel" aria-pressed={selected === index}
            onClick={() => setSelected(index)} title={`${entry.name}: ${fmt(entry.value)} relative units`}
            style={{ ["--heat" as string]: pressureColor(entry.value) }}>
            <small>ch{offset}</small><strong>{fmt(entry.value)}</strong>
          </button>;
        })}
      </div>)}
      <div className="details"><b>{channel.name}</b><span>I: {fmt(channel.raw_i)}</span><span>Q: {fmt(channel.raw_q)}</span><span>Value: {fmt(channel.value)}</span></div>
      <small>Electrical channels are not confirmed physical-zone names. Surface positions belong to the 3D overlay mapping.</small>
    </section>
    <section>
      <h3>Simulation input</h3>
      <small>Requires a fresh true heartbeat on {DEMO_ENABLED_TOPIC}. Injection starts disabled and is never saved in a layout.</small>
      <label><input type="checkbox" checked={injection} disabled={!canInject} onChange={(event) => {
        const enable = event.target.checked && lease.current.available(Date.now()) && canInject;
        injectionRef.current = enable; setInjection(enable);
      }} />Enable simulation input</label>
      <label>{CHANNEL_NAMES[selected]}<input aria-label="Selected channel simulated relative pressure" type="range" min={0} max={1} step={0.01}
        value={draft[selected] ?? 0} disabled={!injection || !canInject} onChange={(event) => {
          const next = draft.slice(); next[selected] = Number(event.target.value); publish(next);
        }} /><output>{(draft[selected] ?? 0).toFixed(2)}</output></label>
      <button disabled={!injection || !canInject} onClick={() => publish(Array(CHANNEL_COUNT).fill(0) as number[])}>Clear all 54 simulation channels</button>
      {error && <p className="error" role="alert">{error}</p>}
    </section>
  </div>;
}

export function initPncTactile(context: PanelExtensionContext): () => void {
  context.setDefaultPanelTitle?.("PNC Tactile Diagnostics");
  const root = createRoot(context.panelElement);
  root.render(<PncTactile context={context} />);
  return () => root.unmount();
}
