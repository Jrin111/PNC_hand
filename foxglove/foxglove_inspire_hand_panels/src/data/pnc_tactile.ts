// Apache-2.0. PNC electrical channel identity is supplied by Keys, never array position.
export const NAMES_TOPIC = "/tactile/tactile_hand_state_broadcaster/names";
export const VALUES_TOPIC = "/tactile/tactile_hand_state_broadcaster/values";
export const OUTPUT_TOPIC = "/pnc/tactile_channels";
export const DEMO_ENABLED_TOPIC = "/pnc_demo/enabled";
export const DEMO_VALUES_TOPIC = "/pnc_demo/tactile_values";
export const CHANNEL_COUNT = 54;
export const STALE_AFTER_MS = 2000;
export const CHANNEL_NAMES = Array.from({ length: CHANNEL_COUNT }, (_, index) =>
  `raa${Math.floor(index / 6)}_ch${index % 6}`);
const INTERFACES = ["raw_i", "raw_q", "value"] as const;

export type TactileChannel = {
  name: string;
  raw_i: number;
  raw_q: number;
  value: number;
  raw_i_known: boolean;
  raw_q_known: boolean;
  value_known: boolean;
};
export type TactileFrame = { names_received: boolean; channels: TactileChannel[] };

function field(message: unknown, key: string): unknown {
  return typeof message === "object" && message !== null
    ? (message as Record<string, unknown>)[key] : undefined;
}

export function unknownFrame(): TactileFrame {
  return {
    names_received: false,
    channels: CHANNEL_NAMES.map((name) => ({
      name, raw_i: NaN, raw_q: NaN, value: NaN,
      raw_i_known: false, raw_q_known: false, value_known: false,
    })),
  };
}

export class TactileDecoder {
  private indexByKey = new Map<string, number>();
  private namesReceived = false;
  private keyCount = 0;

  reset(): void {
    this.indexByKey.clear();
    this.namesReceived = false;
    this.keyCount = 0;
  }

  setNames(message: unknown): void {
    this.reset();
    const keys = field(message, "keys");
    if (!Array.isArray(keys)) return;
    // Values must match every source slot, including unrecognized and duplicate keys.
    this.keyCount = keys.length;
    const duplicates = new Set<string>();
    keys.forEach((key: unknown, index: number) => {
      if (typeof key !== "string" || !/^raa[0-8]_ch[0-5]\/(raw_i|raw_q|value)$/.test(key)) return;
      if (this.indexByKey.has(key)) duplicates.add(key);
      this.indexByKey.set(key, index);
    });
    // A duplicate key is ambiguous, even when both slots currently contain equal numbers.
    duplicates.forEach((key) => this.indexByKey.delete(key));
    this.namesReceived = this.indexByKey.size > 0;
  }

  decode(message: unknown): TactileFrame {
    const result = unknownFrame();
    result.names_received = this.namesReceived;
    const raw = field(message, "values");
    if (!Array.isArray(raw) && !(raw instanceof Float64Array)) return result;
    if (raw.length !== this.keyCount) return result;
    for (const channel of result.channels) {
      for (const iface of INTERFACES) {
        const index = this.indexByKey.get(`${channel.name}/${iface}`);
        const value: unknown = index === undefined ? undefined : raw[index];
        if (typeof value === "number" && Number.isFinite(value)) {
          channel[iface] = value;
          channel[`${iface}_known`] = true;
        }
      }
    }
    return result;
  }
}

// This is a lease from the simulator, not a persisted UI preference.
export class SimulationLease {
  private enabled = false;
  private receivedAt = -Infinity;

  observe(message: unknown, now: number): void {
    this.enabled = field(message, "data") === true;
    this.receivedAt = now;
  }

  reset(): void { this.enabled = false; this.receivedAt = -Infinity; }

  available(now: number): boolean {
    const age = now - this.receivedAt;
    return this.enabled && age >= 0 && age < STALE_AFTER_MS;
  }
}

export function simulationArray(values: readonly number[]): Record<string, unknown> {
  if (values.length !== CHANNEL_COUNT || values.some((value) => !Number.isFinite(value))) {
    throw new Error("Simulation requires exactly 54 finite relative pressure values.");
  }
  return {
    layout: { dim: [{ label: "raa0_ch0..raa8_ch5", size: CHANNEL_COUNT, stride: CHANNEL_COUNT }], data_offset: 0 },
    data: [...values],
  };
}

// Kept identical to pnc_tactile_visualizer/core.py COLOR_STOPS/response_color.
export const UNKNOWN_COLOR = [0.32, 0.34, 0.38, 1] as const;
export const COLOR_STOPS = [
  [0, [0.06, 0.18, 0.55]],
  [0.25, [0, 0.62, 0.88]],
  [0.5, [0.12, 0.82, 0.42]],
  [0.75, [1, 0.79, 0.05]],
  [1, [0.94, 0.12, 0.08]],
] as const;

export function pressureRgba(value: number): readonly [number, number, number, number] {
  if (!Number.isFinite(value)) return UNKNOWN_COLOR;
  const fraction = Math.max(0, Math.min(1, value));
  for (let index = 0; index < COLOR_STOPS.length - 1; index++) {
    const [lo, start] = COLOR_STOPS[index]!;
    const [hi, end] = COLOR_STOPS[index + 1]!;
    if (fraction <= hi) {
      const weight = (fraction - lo) / (hi - lo);
      const lerp = (component: 0 | 1 | 2): number =>
        start[component] + weight * (end[component] - start[component]);
      return [lerp(0), lerp(1), lerp(2), 1];
    }
  }
  return [...COLOR_STOPS[COLOR_STOPS.length - 1]![1], 1];
}

export function pressureColor(value: number): string {
  const [red, green, blue, alpha] = pressureRgba(value);
  return `rgba(${red * 255}, ${green * 255}, ${blue * 255}, ${alpha})`;
}

export const PRESSURE_GRADIENT = `linear-gradient(90deg, ${COLOR_STOPS.map(([position]) =>
  `${pressureColor(position)} ${position * 100}%`).join(", ")})`;
