// Shared visual theme for the Dexterous Hand panels.
// Inspired by ref/dexterous_hands_static_gui (RH56ForcePanel) — dark gradient
// backdrop, neon accents (#62f0c3 green, #ff7f65 alert, #4aa6ff cyan), 16px
// rounded cards.

export const PANEL_CSS = `
.ihc-root {
  --accent: #62f0c3;
  --accent-glow: rgba(98, 240, 195, 0.45);
  --alert: #ff7f65;
  --alert-glow: rgba(255, 127, 101, 0.45);
  --cyan: #4aa6ff;
  --bg-card: rgba(28, 39, 51, 0.72);
  --border: rgba(237, 247, 245, 0.1);
  --text: #edf7f5;
  --text-dim: #9fb2c0;
  --text-mute: #6b7e8c;

  height: 100%;
  min-height: 0;
  display: flex;
  flex-direction: column;
  color: var(--text);
  background:
    radial-gradient(circle at 12% 0%, rgba(54, 132, 150, 0.18), transparent 34%),
    radial-gradient(circle at 88% 12%, rgba(201, 126, 88, 0.10), transparent 30%),
    linear-gradient(145deg, #18212b 0%, #111a24 48%, #1c2530 100%);
  font: 12px/1.45 "DIN Alternate", "Avenir Next Condensed", "Helvetica Neue", "PingFang SC", sans-serif;
  overflow: auto;
}

.ihc-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 12px;
  padding: 6px 12px;
  border-bottom: 1px solid var(--border);
  background:
    linear-gradient(90deg, rgba(255,255,255,0.06), rgba(255,255,255,0.015)),
    rgba(3, 8, 12, 0.26);
}
.ihc-kicker {
  display: inline-flex; align-items: center; gap: 6px;
  margin-bottom: 0;
  color: var(--accent);
  font-size: 9px; font-weight: 700;
  letter-spacing: 0.12em; text-transform: uppercase;
}
.ihc-kicker::before {
  content: ""; width: 6px; height: 6px; border-radius: 50%;
  background: var(--accent);
  box-shadow: 0 0 12px var(--accent-glow);
}
.ihc-title {
  margin: 0;
  font-size: 14px;
  font-weight: 700;
  letter-spacing: -0.015em;
  line-height: 1.15;
}
.ihc-subtitle { display: none; }

.ihc-stats {
  display: flex; align-items: center; gap: 6px;
}
.ihc-stat {
  min-width: 0;
  padding: 4px 8px;
  border: 1px solid var(--border);
  border-radius: 8px;
  background: rgba(255,255,255,0.05);
  box-shadow: inset 0 1px 0 rgba(255,255,255,0.05);
  display: inline-flex; align-items: center; gap: 6px;
}
.ihc-stat-label { color: var(--text-mute); font-size: 9px; font-weight: 700; letter-spacing: 0.08em; text-transform: uppercase; }
.ihc-stat-value { margin-top: 0; font-size: 11px; font-weight: 650; color: var(--text-dim); }
.ihc-stat-value.live { color: var(--accent); }

.ihc-live-toggle {
  display: inline-flex; align-items: center; gap: 6px;
  padding: 4px 10px;
  border: 1px solid var(--border);
  border-radius: 8px;
  background: rgba(255,255,255,0.04);
  color: var(--text-dim);
  font: inherit; font-weight: 700; letter-spacing: 0.04em;
  cursor: pointer;
  transition: 120ms ease;
}
.ihc-live-toggle:hover { border-color: var(--accent); color: var(--text); }
.ihc-live-toggle.on { color: var(--accent); border-color: var(--accent); background: rgba(98,240,195,0.07); }
.ihc-live-dot {
  width: 8px; height: 8px; border-radius: 50%;
  background: var(--text-mute);
}
.ihc-live-toggle.on .ihc-live-dot {
  background: var(--accent); box-shadow: 0 0 10px var(--accent-glow);
}

.ihc-section {
  padding: 12px 16px 8px;
  border-bottom: 1px solid var(--border);
  display: flex;
  flex-direction: column;
  min-height: 0;
  flex: 1 1 auto;
}
.ihc-section.gripper {
  border-bottom: none;
  flex: 0 0 auto;
}
.ihc-section.gripper.tall {
  flex: 1 1 auto;
  min-height: 0;
}
.ihc-section:last-child { padding-bottom: 16px; }

.ihc-section-head {
  display: flex; align-items: center; gap: 12px;
  margin-bottom: 10px;
  flex: 0 0 auto;
}
.ihc-section-title {
  color: var(--text);
  font-size: 13px; font-weight: 700;
  letter-spacing: 0.04em; text-transform: uppercase;
}
.ihc-section-hint { color: var(--text-mute); font-size: 11px; }
.ihc-actions {
  margin-left: auto;
  display: flex; align-items: center; gap: 8px;
}

.ihc-btn {
  padding: 6px 14px;
  border: 1px solid var(--border);
  border-radius: 10px;
  background: rgba(255,255,255,0.05);
  color: var(--text);
  font: inherit; font-weight: 650; letter-spacing: 0.04em;
  cursor: pointer;
  transition: 120ms ease;
}
.ihc-btn:hover { border-color: var(--accent); background: rgba(98,240,195,0.08); }
.ihc-btn.primary {
  border-color: var(--accent);
  background: linear-gradient(135deg, rgba(98,240,195,0.18), rgba(74,166,255,0.10));
  color: var(--accent);
}
.ihc-btn.primary:hover { background: linear-gradient(135deg, rgba(98,240,195,0.28), rgba(74,166,255,0.18)); }
.ihc-btn.ghost { background: transparent; }

.ihf-apply-btn {
  position: relative;
  transition: transform 120ms ease, box-shadow 220ms ease, background 220ms ease, border-color 220ms ease, color 220ms ease;
}
.ihf-apply-btn:active { transform: scale(0.96); }
.ihf-apply-btn.applied {
  border-color: var(--accent);
  background: linear-gradient(135deg, rgba(98,240,195,0.32), rgba(74,166,255,0.18));
  color: var(--accent);
  box-shadow: 0 0 0 0 var(--accent-glow);
  animation: ihf-apply-pulse 0.9s ease-out;
}
@keyframes ihf-apply-pulse {
  0%   { box-shadow: 0 0 0 0 var(--accent-glow); transform: scale(1.0); }
  20%  { box-shadow: 0 0 0 6px rgba(98,240,195,0.25); transform: scale(1.04); }
  100% { box-shadow: 0 0 0 14px rgba(98,240,195,0.0); transform: scale(1.0); }
}

/* Gesture event indicator (Tactile Pads panel). */
.ihc-gesture-btn {
  display: inline-flex; flex-direction: column; align-items: flex-start;
  gap: 2px; min-width: 160px; line-height: 1.1;
  cursor: default; opacity: 1;
}
.ihc-gesture-btn[disabled] { opacity: 1; cursor: default; }
.ihc-gesture-kicker {
  font-size: 9px; font-weight: 700; letter-spacing: 0.08em; text-transform: uppercase;
  color: var(--text-dim);
}
.ihc-gesture-name {
  font-size: 13px; font-weight: 700; color: var(--text);
  font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
}
.ihc-gesture-btn.applied .ihc-gesture-kicker,
.ihc-gesture-btn.applied .ihc-gesture-name { color: var(--accent); }

.ihc-master {
  display: inline-flex; align-items: center; gap: 8px;
  padding: 4px 10px;
  border: 1px solid var(--border);
  border-radius: 10px;
  color: var(--text-dim);
  font-size: 10px; font-weight: 700; letter-spacing: 0.06em; text-transform: uppercase;
}
.ihc-master input[type="range"] { width: 110px; }

.ihc-grid {
  display: grid;
  gap: 10px;
  flex: 1 1 auto;
  min-height: 0;
  align-content: start;
  grid-auto-rows: minmax(0, 1fr);
}
.ihc-grid.joints { grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); }
.ihc-grid.pads   { grid-template-columns: repeat(auto-fit, minmax(170px, 1fr)); gap: 8px; }

.ihc-card {
  position: relative;
  padding: 12px 14px;
  border: 1px solid var(--border);
  border-radius: 16px;
  background: linear-gradient(135deg, rgba(255,255,255,0.06), rgba(255,255,255,0.015)), var(--bg-card);
  box-shadow: 0 14px 30px rgba(0,0,0,0.18), inset 0 1px 0 rgba(255,255,255,0.05);
  transition: 120ms ease;
  display: flex;
  flex-direction: column;
  justify-content: center;
  min-height: 0;
}
.ihc-card:hover { border-color: rgba(110, 241, 201, 0.32); transform: translateY(-1px); }
.ihc-card.alert { border-color: rgba(255, 127, 101, 0.55); box-shadow: 0 14px 30px rgba(0,0,0,0.18), 0 0 26px rgba(255,127,101,0.18); }
.ihc-card.contact { border-color: rgba(98,240,195,0.5); box-shadow: 0 14px 30px rgba(0,0,0,0.18), 0 0 22px rgba(98,240,195,0.18); }
.ihc-card.fake { background: linear-gradient(135deg, rgba(74,166,255,0.10), rgba(98,240,195,0.04)), var(--bg-card); }

/* Joint card — compact layout (one row per joint stays short). */
.ihc-card.joint { padding: 8px 12px; }
.ihc-card.joint .ihc-card-head { margin-bottom: 2px; }
.ihc-card.joint .ihc-slider.position { height: 18px; }
.ihc-card.joint .ihc-slider.position::-webkit-slider-runnable-track { height: 6px; }
.ihc-card.joint .ihc-slider.position::-moz-range-track { height: 6px; }
.ihc-card.joint .ihc-slider.position::-webkit-slider-thumb { width: 16px; height: 16px; margin-top: -5px; border-width: 2px; }
.ihc-card.joint .ihc-slider.position::-moz-range-thumb { width: 16px; height: 16px; border-width: 2px; }
.ihc-card.joint .ihc-readout-row { gap: 10px; margin: 2px 0 4px; }
.ihc-card.joint .ihc-readout-label { font-size: 8px; }
.ihc-card.joint .ihc-readout-value { font-size: 12px; }
.ihc-card.joint .ihc-fader { margin-top: 4px; padding: 4px 6px; gap: 6px; grid-template-columns: 52px 1fr 52px; }
.ihc-card.joint .ihc-fader-label { font-size: 9px; }
.ihc-card.joint .ihc-fader-readout { font-size: 11px; padding: 1px 6px; min-width: 48px; }
.ihc-card.joint .ihc-slider.threshold { height: 14px; }
.ihc-card.joint .ihc-slider.threshold::-webkit-slider-thumb { width: 12px; height: 12px; margin-top: -4px; }
.ihc-card.joint .ihc-slider.threshold::-moz-range-thumb { width: 12px; height: 12px; }

/* A short Console panel scrolls instead of compressing controls into each other.
   Scope this to Console: Force and Gripper keep their existing flexible layout. */
.ihc-root.console > .ihc-header,
.ihc-root.console > .ihc-section { flex: 0 0 auto; }
.ihc-root.console .ihc-section-head,
.ihc-root.console .ihc-actions { flex-wrap: wrap; }
.ihc-root.console .ihc-grid.joints {
  flex: 0 0 auto;
  grid-auto-rows: minmax(104px, auto);
}
.ihc-root.console .ihc-card.joint {
  box-sizing: border-box;
  min-height: 104px;
}
.ihc-root.console .ihc-presets { flex-shrink: 0; }

.ihc-card-head {
  display: flex; align-items: center; justify-content: space-between;
  margin-bottom: 10px;
}
.ihc-card-title {
  color: var(--text); font-size: 13px; font-weight: 700; letter-spacing: 0.02em;
}
.ihc-card-tag {
  color: var(--text-mute); font-size: 10px; font-weight: 700; letter-spacing: 0.08em;
}

.ihc-track {
  position: relative;
  height: 14px;
  border-radius: 999px;
  overflow: hidden;
  background: rgba(2, 6, 10, 0.48);
  box-shadow: inset 0 0 0 1px var(--border), inset 0 6px 14px rgba(0,0,0,0.24);
}
.ihc-track.tall { height: 22px; }
.ihc-track-live {
  position: absolute; inset: 0 auto 0 0;
  height: 100%;
  background: linear-gradient(90deg, rgba(74,166,255,0.6), var(--accent));
  box-shadow: 0 0 14px var(--accent-glow);
  transition: width 120ms ease;
}
.ihc-track-live.amp { background: linear-gradient(90deg, var(--accent) 0%, #ffd76e 70%, var(--alert) 100%); }
.ihc-track-target {
  position: absolute; top: -3px; bottom: -3px;
  width: 2px;
  background: #fff;
  box-shadow: 0 0 8px rgba(255,255,255,0.85);
  border-radius: 2px;
}

.ihc-readout-row {
  display: flex; gap: 12px;
  margin-top: 10px; margin-bottom: 8px;
}
.ihc-readout { display: flex; flex-direction: column; }
.ihc-readout-label { color: var(--text-mute); font-size: 9px; font-weight: 700; letter-spacing: 0.08em; text-transform: uppercase; }
.ihc-readout-value {
  font-variant-numeric: tabular-nums;
  font-size: 16px; font-weight: 700; color: var(--text);
}
.ihc-readout-value.primary { color: var(--accent); }
.ihc-readout-value.alert { color: var(--alert); }

/* Sliders — restyled native range */
.ihc-slider {
  width: 100%;
  appearance: none; -webkit-appearance: none;
  height: 18px;
  background: transparent;
  cursor: pointer;
}
.ihc-slider::-webkit-slider-runnable-track {
  height: 4px;
  background: linear-gradient(90deg, rgba(74,166,255,0.5), var(--accent));
  border-radius: 999px;
}
.ihc-slider::-moz-range-track {
  height: 4px;
  background: linear-gradient(90deg, rgba(74,166,255,0.5), var(--accent));
  border-radius: 999px;
}
.ihc-slider::-webkit-slider-thumb {
  -webkit-appearance: none;
  width: 16px; height: 16px;
  margin-top: -6px;
  border-radius: 50%;
  background: #fff;
  border: 2px solid var(--accent);
  box-shadow: 0 0 10px var(--accent-glow);
  cursor: pointer;
}
.ihc-slider::-moz-range-thumb {
  width: 16px; height: 16px;
  border-radius: 50%;
  background: #fff;
  border: 2px solid var(--accent);
  box-shadow: 0 0 10px var(--accent-glow);
  cursor: pointer;
}
.ihc-slider.threshold::-webkit-slider-runnable-track { background: linear-gradient(90deg, var(--accent), var(--alert)); }
.ihc-slider.threshold::-moz-range-track { background: linear-gradient(90deg, var(--accent), var(--alert)); }
.ihc-slider.threshold::-webkit-slider-thumb { border-color: var(--alert); box-shadow: 0 0 10px var(--alert-glow); }
.ihc-slider.threshold::-moz-range-thumb { border-color: var(--alert); box-shadow: 0 0 10px var(--alert-glow); }

/* Joint position slider: thumb = target; the runnable-track is filled up to
   --live (live joint position) with the accent gradient, and the remainder
   is a dim trough so live != target is immediately visible. */
.ihc-slider.position {
  --live: 0%;
  height: 26px;
}
.ihc-slider.position::-webkit-slider-runnable-track {
  height: 10px;
  border-radius: 999px;
  background:
    linear-gradient(90deg,
      rgba(74,166,255,0.85) 0%,
      var(--accent) var(--live),
      rgba(2,6,10,0.55) var(--live),
      rgba(2,6,10,0.55) 100%);
  box-shadow: inset 0 0 0 1px var(--border), inset 0 4px 10px rgba(0,0,0,0.3), 0 0 14px var(--accent-glow);
}
.ihc-slider.position::-moz-range-track {
  height: 10px;
  border-radius: 999px;
  background:
    linear-gradient(90deg,
      rgba(74,166,255,0.85) 0%,
      var(--accent) var(--live),
      rgba(2,6,10,0.55) var(--live),
      rgba(2,6,10,0.55) 100%);
  box-shadow: inset 0 0 0 1px var(--border), inset 0 4px 10px rgba(0,0,0,0.3), 0 0 14px var(--accent-glow);
}
.ihc-slider.position::-webkit-slider-thumb {
  width: 22px; height: 22px;
  margin-top: -6px;
  border: 3px solid var(--accent);
  box-shadow: 0 0 14px var(--accent-glow), 0 4px 10px rgba(0,0,0,0.4);
  cursor: grab;
}
.ihc-slider.position::-webkit-slider-thumb:active { cursor: grabbing; transform: scale(1.06); }
.ihc-slider.position::-moz-range-thumb {
  width: 22px; height: 22px;
  border: 3px solid var(--accent);
  box-shadow: 0 0 14px var(--accent-glow), 0 4px 10px rgba(0,0,0,0.4);
  cursor: grab;
}

.ihc-fader {
  display: grid;
  grid-template-columns: 64px 1fr 64px;
  align-items: center; gap: 8px;
  margin-top: 8px;
  padding: 6px 8px;
  border-radius: 10px;
  background: rgba(0,0,0,0.18);
  border: 1px solid var(--border);
}
.ihc-fader-label {
  color: var(--text-mute); font-size: 10px; font-weight: 700; letter-spacing: 0.08em; text-transform: uppercase;
}
.ihc-fader-readout {
  text-align: right;
  padding: 2px 8px;
  min-width: 64px;
  border-radius: 6px;
  font-variant-numeric: tabular-nums;
  font-size: 12px; font-weight: 700;
  color: var(--text);
  background-clip: padding-box;
  border: 1px solid var(--border);
}

.ihc-presets {
  display: flex; flex-wrap: wrap; gap: 6px;
  margin-top: 12px;
}
.ihc-preset {
  flex: 1 1 80px;
  padding: 8px 10px;
  border: 1px solid var(--border);
  border-radius: 10px;
  background: rgba(255,255,255,0.04);
  color: var(--text);
  font: inherit; font-size: 12px; font-weight: 700; letter-spacing: 0.06em; text-transform: uppercase;
  cursor: pointer;
  transition: 120ms ease;
}
.ihc-preset:hover {
  border-color: var(--accent);
  background: linear-gradient(135deg, rgba(98,240,195,0.18), rgba(74,166,255,0.06));
  color: var(--accent);
  transform: translateY(-1px);
}

.ihc-led {
  width: 14px; height: 14px;
  border-radius: 50%;
  background: rgba(255,255,255,0.08);
  border: 1px solid var(--border);
  box-shadow: inset 0 1px 2px rgba(0,0,0,0.4);
  transition: 120ms ease;
}
.ihc-led.big { width: 22px; height: 22px; }
.ihc-led.on {
  background: var(--accent);
  border-color: var(--accent);
  box-shadow: 0 0 12px var(--accent-glow), inset 0 0 4px rgba(255,255,255,0.3);
}

.ihc-pad-head {
  display: flex; align-items: center; gap: 12px;
  margin-bottom: 12px;
}
.ihc-pad-name {
  flex: 1;
  color: var(--text);
  font-size: 16px; font-weight: 700;
  letter-spacing: 0.02em;
  text-align: left;
}
.ihc-pad-state {
  font-size: 11px; font-weight: 800;
  letter-spacing: 0.12em;
  color: var(--text-mute);
  padding: 3px 9px;
  border-radius: 999px;
  border: 1px solid var(--border);
  background: rgba(0,0,0,0.18);
}
.ihc-pad-state.on {
  color: var(--accent);
  border-color: var(--accent);
  background: rgba(98,240,195,0.10);
  box-shadow: 0 0 14px var(--accent-glow);
}

/* Combined pad tile: real-LED + label + source badge + FAKE pill all on one
   clickable button. Card chrome (border + bg) reflects the current state:
   real → green tint, fake → cyan tint, both → blended. */
.ihc-pad-tile {
  --tile-border: var(--border);
  --tile-tint: rgba(255,255,255,0.04);
  --tile-glow: transparent;
  display: grid;
  grid-template-columns: auto 1fr auto;
  grid-template-rows: auto auto;
  grid-template-areas:
    "led name pill"
    "led source pill";
  align-items: center;
  column-gap: 10px;
  row-gap: 0;
  padding: 6px 10px;
  border: 1px solid var(--tile-border);
  border-radius: 12px;
  background: linear-gradient(135deg, var(--tile-tint), rgba(255,255,255,0.015)), var(--bg-card);
  box-shadow: 0 8px 18px rgba(0,0,0,0.18), inset 0 1px 0 rgba(255,255,255,0.05), 0 0 16px var(--tile-glow);
  color: var(--text);
  text-align: left;
  font: inherit;
  cursor: pointer;
  transition: 120ms ease;
}
.ihc-pad-tile > .ihc-led.big { width: 14px; height: 14px; }
.ihc-pad-tile > .ihc-pad-name { font-size: 12px; font-weight: 700; }
.ihc-pad-tile > .ihc-pad-source { font-size: 9px; }
.ihc-pad-tile > .ihc-pad-fake-pill { font-size: 9px; padding: 3px 7px; }
.ihc-pad-tile:hover { transform: translateY(-1px); border-color: rgba(110, 241, 201, 0.32); }
.ihc-pad-tile:focus-visible { outline: 2px solid var(--cyan); outline-offset: 2px; }
.ihc-pad-tile.real {
  --tile-tint: rgba(98,240,195,0.16);
  --tile-border: rgba(98,240,195,0.55);
  --tile-glow: rgba(98,240,195,0.18);
}
.ihc-pad-tile.fake {
  --tile-tint: rgba(74,166,255,0.18);
  --tile-border: var(--cyan);
  --tile-glow: rgba(74,166,255,0.22);
}
.ihc-pad-tile.real.fake {
  --tile-tint: linear-gradient(135deg, rgba(98,240,195,0.18), rgba(74,166,255,0.18));
}
.ihc-pad-tile > .ihc-led { grid-area: led; }
.ihc-pad-tile > .ihc-pad-name { grid-area: name; }
.ihc-pad-tile > .ihc-pad-source { grid-area: source; }
.ihc-pad-tile > .ihc-pad-fake-pill { grid-area: pill; }

.ihc-pad-source {
  font-size: 10px; font-weight: 700;
  letter-spacing: 0.14em; text-transform: uppercase;
  color: var(--text-mute);
}
.ihc-pad-tile.real .ihc-pad-source { color: var(--accent); }
.ihc-pad-tile.fake:not(.real) .ihc-pad-source { color: var(--cyan); }
.ihc-pad-tile.real.fake .ihc-pad-source {
  background: linear-gradient(90deg, var(--accent), var(--cyan));
  -webkit-background-clip: text;
  background-clip: text;
  color: transparent;
}

.ihc-pad-fake-pill {
  font-size: 10px; font-weight: 800;
  letter-spacing: 0.14em; text-transform: uppercase;
  color: var(--text-mute);
  padding: 5px 10px;
  border-radius: 999px;
  border: 1px dashed var(--border);
  background: transparent;
}
.ihc-pad-tile.fake .ihc-pad-fake-pill {
  color: var(--cyan);
  border-style: solid;
  border-color: var(--cyan);
  background: rgba(74,166,255,0.10);
  box-shadow: 0 0 14px rgba(74,166,255,0.18);
}

.ihc-fake-btn {
  display: block;
  width: 100%;
  margin-top: 6px;
  padding: 6px 10px;
  border: 1px dashed var(--border);
  border-radius: 10px;
  background: transparent;
  color: var(--text-mute);
  font: inherit; font-size: 11px; font-weight: 700; letter-spacing: 0.08em; text-transform: uppercase;
  cursor: pointer;
  transition: 120ms ease;
}
.ihc-fake-btn:hover { border-color: var(--cyan); color: var(--cyan); }
.ihc-fake-btn.on {
  border-style: solid;
  border-color: var(--cyan);
  color: var(--cyan);
  background: rgba(74,166,255,0.10);
  box-shadow: 0 0 18px rgba(74,166,255,0.18);
}

/* ------------------------------------------------------------------------- */
/* Force monitor — compact vertical-bar layout (Dexterous Hand Force panel).*/
/* ------------------------------------------------------------------------- */
.ihc-section.force {
  flex: 1 1 auto;
  display: flex;
  flex-direction: column;
  min-height: 0;
  border-bottom: none;
}
.ihf-bars {
  flex: 1 1 auto;
  display: grid;
  grid-template-columns: 28px repeat(6, minmax(56px, 1fr));
  align-items: stretch;
  gap: 6px;
  padding: 8px 4px 6px;
  border: 1px solid var(--border);
  border-radius: 14px;
  background: linear-gradient(135deg, rgba(255,255,255,0.04), rgba(255,255,255,0.01)), var(--bg-card);
  box-shadow: inset 0 1px 0 rgba(255,255,255,0.04);
  min-height: 140px;
}
.ihf-bar-axis {
  display: flex;
  flex-direction: column;
  align-items: stretch;
  min-height: 0;
  color: var(--text-mute);
  font-size: 9px; font-weight: 700; letter-spacing: 0.08em;
  font-variant-numeric: tabular-nums;
}
.ihf-bar-axis-track {
  position: relative;
  flex: 1 1 auto;
  min-height: 0;
  margin: 2px 0;
}
.ihf-bar-axis-tick {
  position: absolute;
  right: 2px;
  transform: translateY(50%);
  line-height: 1;
  white-space: nowrap;
  color: var(--text-mute);
  font-size: 9px; font-weight: 700; letter-spacing: 0.08em;
  font-variant-numeric: tabular-nums;
}
.ihf-axis-col > .ihf-bar-value-top,
.ihf-axis-col > .ihf-bar-name,
.ihf-axis-col > .ihf-bar-thresh-readout,
.ihf-axis-col > .ihf-thresh-row {
  visibility: hidden;
}
.ihf-axis-col > .ihf-bar-track.ihf-axis-track {
  background: none;
  box-shadow: none;
  overflow: visible;
}
.ihf-bar-col {
  display: flex; flex-direction: column;
  align-items: stretch;
  min-width: 0;
  min-height: 0;
}
.ihf-bar-value-top {
  display: flex; align-items: baseline; justify-content: center; gap: 2px;
  height: 16px;
  font-variant-numeric: tabular-nums;
}
.ihf-bar-value {
  font-size: 13px; font-weight: 800;
  color: var(--accent);
  text-shadow: 0 0 10px var(--accent-glow);
}
.ihf-bar-value.alert {
  color: var(--alert);
  text-shadow: 0 0 10px var(--alert-glow);
}
.ihf-bar-unit {
  font-size: 9px; font-weight: 700;
  color: var(--text-mute);
  letter-spacing: 0.08em; text-transform: uppercase;
}
.ihf-bar-track {
  flex: 1 1 auto;
  position: relative;
  min-height: 60px;
  margin: 2px 6px;
  border-radius: 8px;
  background:
    linear-gradient(180deg, rgba(2,6,10,0.55), rgba(2,6,10,0.35));
  box-shadow: inset 0 0 0 1px var(--border), inset 0 4px 10px rgba(0,0,0,0.35);
  overflow: hidden;
}
.ihf-bar-zero {
  position: absolute;
  left: 0; right: 0;
  height: 1px;
  background: rgba(237, 247, 245, 0.35);
  pointer-events: none;
}
.ihf-bar-fill {
  position: absolute;
  left: 4px; right: 4px;
  border-radius: 4px;
  transition: height 120ms ease;
}
.ihf-bar-fill.pos {
  background: linear-gradient(180deg, var(--accent) 0%, rgba(74,166,255,0.85) 100%);
  box-shadow: 0 0 14px var(--accent-glow);
}
.ihf-bar-fill.neg {
  background: linear-gradient(180deg, rgba(74,166,255,0.85) 0%, var(--cyan) 100%);
  box-shadow: 0 0 12px rgba(74,166,255,0.35);
}
.ihf-bar-fill.alert {
  background: linear-gradient(180deg, var(--alert) 0%, #ffd76e 100%) !important;
  box-shadow: 0 0 16px var(--alert-glow) !important;
}
.ihf-bar-thresh {
  position: absolute;
  left: 0; right: 0;
  height: 0;
  border-top: 1px dashed var(--alert);
  box-shadow: 0 0 6px var(--alert-glow);
  pointer-events: none;
}
.ihf-bar-track.draggable {
  cursor: ns-resize;
  touch-action: none;
}
.ihf-bar-track.draggable:hover {
  box-shadow: inset 0 0 0 1px rgba(255,127,101,0.45), inset 0 4px 10px rgba(0,0,0,0.35);
}
.ihf-bar-track.draggable:focus { outline: none; }
.ihf-bar-track.draggable:focus-visible {
  box-shadow: inset 0 0 0 2px var(--accent), inset 0 4px 10px rgba(0,0,0,0.35), 0 0 12px var(--accent-glow);
}
.ihf-bar-thresh-off {
  position: absolute;
  top: 1px; left: 0; right: 0;
  text-align: center;
  font-size: 9px;
  color: var(--alert);
  text-shadow: 0 0 6px var(--alert-glow);
  pointer-events: none;
}
.ihf-bar-col.alert .ihf-bar-track {
  box-shadow: inset 0 0 0 1px rgba(255,127,101,0.55), inset 0 4px 10px rgba(0,0,0,0.35), 0 0 18px rgba(255,127,101,0.18);
}
.ihf-bar-name {
  margin-top: 4px;
  text-align: center;
  font-size: 10px; font-weight: 700;
  letter-spacing: 0.04em;
  color: var(--text-dim);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.ihf-bar-thresh-readout {
  margin-top: 2px;
  text-align: center;
  font-size: 11px; font-weight: 800;
  font-variant-numeric: tabular-nums;
  color: var(--alert);
  text-shadow: 0 0 6px var(--alert-glow);
  white-space: nowrap;
}
.ihf-thresh-row {
  display: grid;
  grid-template-columns: 22px 1fr 28px;
  align-items: center;
  gap: 4px;
  margin-top: 4px;
  padding: 2px 4px;
  border-radius: 8px;
  background: rgba(0,0,0,0.18);
  border: 1px solid var(--border);
}
.ihf-thresh-label {
  color: var(--text-mute);
  font-size: 8px; font-weight: 700;
  letter-spacing: 0.08em; text-transform: uppercase;
  text-align: left;
}
.ihf-thresh-value {
  font-variant-numeric: tabular-nums;
  font-size: 10px; font-weight: 700;
  color: var(--text);
  text-align: right;
}
.ihf-thresh-input.ihc-slider.threshold { height: 14px; }
.ihf-thresh-input.ihc-slider.threshold::-webkit-slider-thumb { width: 12px; height: 12px; margin-top: -4px; }
.ihf-thresh-input.ihc-slider.threshold::-moz-range-thumb { width: 12px; height: 12px; }
.ihf-master {
  display: grid;
  grid-template-columns: auto 1fr auto;
  align-items: center;
  gap: 10px;
  margin-top: 8px;
  padding: 6px 12px;
  border: 1px solid var(--border);
  border-radius: 10px;
  background: rgba(255,255,255,0.04);
}
.ihf-master-label {
  color: var(--text-dim);
  font-size: 10px; font-weight: 700;
  letter-spacing: 0.08em; text-transform: uppercase;
}
.ihf-master-hint {
  color: var(--text-mute);
  font-size: 10px;
}
.ihf-master input[type="range"] { width: 100%; }

.ihc-gripper-row {
  display: grid;
  grid-template-columns: 1fr 1fr auto;
  align-items: center; gap: 12px;
}
.ihc-gripper-row.single { grid-template-columns: 1fr auto; }
.ihc-gripper-row.tall {
  flex: 1 1 auto;
  min-height: 0;
  height: 100%;
  align-items: stretch;
  align-content: stretch;
}
.ihc-gripper-field {
  display: grid;
  grid-template-columns: 90px 1fr 70px;
  align-items: center; gap: 8px;
  padding: 8px 12px;
  border: 1px solid var(--border);
  border-radius: 12px;
  background: rgba(255,255,255,0.04);
}
.ihc-gripper-field.wide { grid-template-columns: 90px 1fr 80px; }
.ihc-gripper-field.tall {
  align-items: center;
  align-content: center;
  padding: 14px 18px;
  min-height: 0;
  height: 100%;
}
.ihc-gripper-row.tall > .ihc-btn {
  height: 100%;
  align-self: stretch;
}
.ihc-gripper-field.tall > span:first-child { font-size: 12px; }
.ihc-gripper-field.tall .ihc-gripper-readout { font-size: 18px; }
.ihc-gripper-field.tall input[type="range"] { height: 28px; }
.ihc-gripper-field.tall input[type="range"]::-webkit-slider-runnable-track { height: 8px; }
.ihc-gripper-field.tall input[type="range"]::-moz-range-track { height: 8px; }
.ihc-gripper-field.tall input[type="range"]::-webkit-slider-thumb { width: 22px; height: 22px; margin-top: -7px; border-width: 3px; }
.ihc-gripper-field.tall input[type="range"]::-moz-range-thumb { width: 22px; height: 22px; border-width: 3px; }
.ihc-gripper-field > span:first-child {
  color: var(--text-mute); font-size: 10px; font-weight: 700; letter-spacing: 0.06em; text-transform: uppercase;
}
.ihc-gripper-readout {
  text-align: right;
  font-variant-numeric: tabular-nums;
  font-size: 13px; font-weight: 700; color: var(--text);
}
.ihc-gripper-field input[type="range"] { width: 100%; height: 18px; -webkit-appearance: none; appearance: none; background: transparent; }
.ihc-gripper-field input[type="range"]::-webkit-slider-runnable-track { height: 4px; border-radius: 999px; background: linear-gradient(90deg, rgba(74,166,255,0.5), var(--accent)); }
.ihc-gripper-field input[type="range"]::-moz-range-track { height: 4px; border-radius: 999px; background: linear-gradient(90deg, rgba(74,166,255,0.5), var(--accent)); }
.ihc-gripper-field input[type="range"]::-webkit-slider-thumb { -webkit-appearance: none; width: 14px; height: 14px; margin-top: -5px; border-radius: 50%; background: #fff; border: 2px solid var(--accent); }
.ihc-gripper-field input[type="range"]::-moz-range-thumb { width: 14px; height: 14px; border-radius: 50%; background: #fff; border: 2px solid var(--accent); }

/* ------------------------------------------------------------------------- */
/* Light theme overrides — applied when Foxglove reports colorScheme=light.  */
/* ------------------------------------------------------------------------- */
.ihc-root[data-theme="light"] {
  --accent: #2eb88a;
  --accent-glow: rgba(46, 184, 138, 0.35);
  --alert: #e04b32;
  --alert-glow: rgba(224, 75, 50, 0.35);
  --cyan: #2c7be5;
  --bg-card: rgba(255, 255, 255, 0.92);
  --border: rgba(20, 30, 40, 0.14);
  --text: #1a2230;
  --text-dim: #4f6275;
  --text-mute: #7c8a9a;

  background:
    radial-gradient(circle at 12% 0%, rgba(46, 184, 138, 0.10), transparent 36%),
    radial-gradient(circle at 88% 12%, rgba(44, 123, 229, 0.08), transparent 32%),
    linear-gradient(145deg, #f5f7fa 0%, #eef1f5 48%, #e7ecf2 100%);
}
.ihc-root[data-theme="light"] .ihc-header {
  background:
    linear-gradient(90deg, rgba(255,255,255,0.7), rgba(255,255,255,0.3)),
    rgba(20, 30, 40, 0.04);
}
.ihc-root[data-theme="light"] .ihc-stat,
.ihc-root[data-theme="light"] .ihc-live-toggle {
  background: rgba(255,255,255,0.7);
  box-shadow: inset 0 1px 0 rgba(255,255,255,0.9);
}
.ihc-root[data-theme="light"] .ihc-card {
  background:
    linear-gradient(135deg, rgba(255,255,255,0.55), rgba(255,255,255,0.2)),
    var(--bg-card);
  box-shadow: 0 8px 20px rgba(20,30,40,0.08), inset 0 1px 0 rgba(255,255,255,0.85);
}
.ihc-root[data-theme="light"] .ihc-pad-tile,
.ihc-root[data-theme="light"] .ihc-preset {
  background: rgba(255,255,255,0.7);
}
.ihc-root[data-theme="light"] .ihc-btn {
  background: rgba(255,255,255,0.7);
}
.ihc-root[data-theme="light"] .ihc-btn.primary {
  background: linear-gradient(135deg, rgba(46,184,138,0.20), rgba(44,123,229,0.12));
}
.ihc-root[data-theme="light"] .ihc-btn.primary:hover {
  background: linear-gradient(135deg, rgba(46,184,138,0.32), rgba(44,123,229,0.18));
}
.ihc-root[data-theme="light"] .ihc-thumb-glow {
  box-shadow: 0 0 6px rgba(20,30,40,0.35);
}
`;
