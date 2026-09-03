const { test } = require("node:test");
const assert = require("node:assert/strict");
const { TactileDecoder, SimulationLease, CHANNEL_NAMES, STALE_AFTER_MS,
  simulationArray, pressureColor, pressureRgba, NAMES_TOPIC, VALUES_TOPIC } = require("../.test-dist/data/pnc_tactile.js");
const { registerPncTactileCombined } = require("../.test-dist/converters/pnc_tactile_combined.js");
const { registerJointStateCombined } = require("../.test-dist/converters/joint_state_combined.js");

test("54 channels require named keys; no fixed-order fallback before names", () => {
  const decoder = new TactileDecoder();
  const frame = decoder.decode({ values: Array(162).fill(1) });
  assert.equal(frame.channels.length, 54);
  assert.equal(frame.names_received, false);
  assert(frame.channels.every((channel) => Number.isNaN(channel.value) && !channel.value_known));
});

test("shuffled complete keys map all 162 measurements by identity", () => {
  const keys = CHANNEL_NAMES.flatMap((name) => ["raw_i", "raw_q", "value"].map((field) => `${name}/${field}`)).reverse();
  const expected = new Map(keys.map((key, index) => [key, index + 0.5]));
  const decoder = new TactileDecoder();
  decoder.setNames({ keys });
  const frame = decoder.decode({ values: Float64Array.from(keys.map((key) => expected.get(key))) });
  for (const channel of frame.channels) {
    for (const field of ["raw_i", "raw_q", "value"]) {
      assert.equal(channel[field], expected.get(`${channel.name}/${field}`));
      assert.equal(channel[`${field}_known`], true);
    }
  }
});

test("missing, duplicate, malformed, and nonfinite slots stay Unknown; real zero remains known", () => {
  const decoder = new TactileDecoder();
  decoder.setNames({ keys: ["raa0_ch0/value", "raa0_ch0/raw_i", "raa0_ch0/raw_q",
    "raa0_ch1/value", "raa0_ch1/value", "raa0_ch2/value", "raa0_ch3/value", "raa0_ch4/value"] });
  const channels = decoder.decode({ values: [0, NaN, Infinity, 10, 20, "0", -Infinity, undefined] }).channels;
  assert.equal(channels[0].value, 0);
  assert.equal(channels[0].value_known, true);
  assert.equal(channels[0].raw_i_known, false);
  assert.equal(channels[0].raw_q_known, false);
  assert(channels.slice(1, 6).every((channel) => Number.isNaN(channel.value) && !channel.value_known));
});

test("short and long values frames make every measurement Unknown for arrays and Float64Array", () => {
  const decoder = new TactileDecoder();
  decoder.setNames({ keys: ["raa0_ch0/raw_i", "raa0_ch0/raw_q", "raa0_ch0/value"] });
  for (const values of [[1, 2], [1, 2, 3, 4]]) {
    for (const raw of [values, Float64Array.from(values)]) {
      const frame = decoder.decode({ values: raw });
      assert.equal(frame.names_received, true);
      assert(frame.channels.every((channel) => ["raw_i", "raw_q", "value"].every((field) =>
        Number.isNaN(channel[field]) && !channel[`${field}_known`])));
    }
  }
});

test("frame length uses all source keys while valid subsets and unrelated fields remain usable", () => {
  const decoder = new TactileDecoder();
  decoder.setNames({ keys: ["raa0_ch0/value", "unrecognized", null,
    "raa0_ch1/value", "raa0_ch1/value", "raa0_ch2/value"] });
  const channels = decoder.decode({ values: [0.2, 1, 2, 0.4, 0.4, 0.8] }).channels;
  assert.equal(channels[0].value, 0.2);
  assert.equal(channels[0].value_known, true);
  assert.equal(channels[0].raw_i_known, false);
  assert.equal(channels[1].value_known, false);
  assert.equal(channels[2].value, 0.8);
  assert.equal(channels[2].value_known, true);
  // Only two unique recognized keys survive; their count is not the source frame length.
  assert(decoder.decode({ values: [0.2, 0.8] }).channels.every((channel) => !channel.value_known));
});

test("names reconfiguration rejects an older frame of a different length and accepts the new schema", () => {
  const decoder = new TactileDecoder();
  decoder.setNames({ keys: ["raa0_ch0/value", "raa0_ch1/value"] });
  assert.equal(decoder.decode({ values: [0.2, 0.8] }).channels[1].value, 0.8);
  decoder.setNames({ keys: ["raa8_ch5/value"] });
  const stale = decoder.decode({ values: Float64Array.from([0.2, 0.8]) });
  assert(stale.channels.every((channel) => !channel.value_known));
  const current = decoder.decode({ values: Float64Array.from([0.6]) });
  assert.equal(current.channels[0].value_known, false);
  assert.equal(current.channels[53].value, 0.6);
  assert.equal(current.channels[53].value_known, true);
});

test("new names replace the previous map; reset/reconnect cannot reuse old indexing", () => {
  const decoder = new TactileDecoder();
  decoder.setNames({ keys: ["raa0_ch0/value"] });
  decoder.setNames({ keys: ["raa8_ch5/value"] });
  let frame = decoder.decode({ values: [0.75] });
  assert.equal(frame.channels[0].value_known, false);
  assert.equal(frame.channels[53].value, 0.75);
  decoder.reset();
  frame = decoder.decode({ values: [0.75] });
  assert.equal(frame.channels[53].value_known, false);
  decoder.setNames(null);
  assert.equal(decoder.decode(null).names_received, false);
});

test("converter clears prior values on new names and old mapping on backwards playback", () => {
  let registration;
  registerPncTactileCombined({ registerMessageConverter: (value) => { registration = value; } });
  const convert = registration.create();
  const event = (topic, message, sec) => ({ topic, message, receiveTime: { sec, nsec: 0 } });
  convert(event(NAMES_TOPIC, { keys: ["raa0_ch0/value"] }, 10));
  assert.equal(convert(event(VALUES_TOPIC, { values: [0.5] }, 11)).channels[0].value, 0.5);
  assert.equal(convert(event(NAMES_TOPIC, { keys: ["raa0_ch1/value"] }, 12)).channels[0].value_known, false);
  assert.equal(convert(event(VALUES_TOPIC, { values: [0.7] }, 2)).channels[1].value_known, false);
});

test("simulation authorization needs a fresh literal true heartbeat and expires or resets", () => {
  const lease = new SimulationLease();
  assert.equal(lease.available(0), false);
  for (const message of [undefined, {}, { data: "true" }, { data: 1 }, { data: false }]) {
    lease.observe(message, 100);
    assert.equal(lease.available(101), false);
  }
  lease.observe({ data: true }, 100);
  assert.equal(lease.available(99), false);
  assert.equal(lease.available(100 + STALE_AFTER_MS - 1), true);
  assert.equal(lease.available(100 + STALE_AFTER_MS), false);
  lease.observe({ data: true }, 3000);
  lease.reset();
  assert.equal(lease.available(3001), false);
});

test("simulation commands are exactly 54 finite values; color never treats Unknown as zero", () => {
  const values = Array(54).fill(0); values[53] = 0.6;
  assert.deepEqual(simulationArray(values).data, values);
  assert.equal(simulationArray(values).layout.dim[0].size, 54);
  assert.throws(() => simulationArray(Array(53).fill(0)));
  assert.throws(() => simulationArray([...Array(53).fill(0), NaN]));
  assert.notEqual(pressureColor(NaN), pressureColor(0));
  assert.notEqual(pressureColor(0.2), pressureColor(0.8));
});

test("position-only joint state keeps missing force and velocity unknown", () => {
  let registration;
  registerJointStateCombined({ registerMessageConverter: (value) => { registration = value; } });
  const result = registration.create()({ message: {
    joint_names: ["index_proximal_joint"],
    interface_values: [{ interface_names: ["position"], values: [0.4] }],
  } });
  const index = result.joints.find((joint) => joint.name === "index_proximal_joint");
  assert.equal(index.position, 0.4);
  assert(Number.isNaN(index.force)); assert(Number.isNaN(index.velocity));
  assert(Number.isNaN(result.joints[0].position));
});

test("diagnostic palette matches the 3D core stops, interpolation, clamping and unknown gray", () => {
  const near = (actual, expected) => actual.forEach((value, index) => assert(Math.abs(value - expected[index]) < 1e-12));
  near(pressureRgba(0), [0.06, 0.18, 0.55, 1]);
  near(pressureRgba(0.25), [0, 0.62, 0.88, 1]);
  near(pressureRgba(0.5), [0.12, 0.82, 0.42, 1]);
  near(pressureRgba(0.75), [1, 0.79, 0.05, 1]);
  near(pressureRgba(1), [0.94, 0.12, 0.08, 1]);
  near(pressureRgba(0.125), [0.03, 0.4, 0.715, 1]);
  near(pressureRgba(-10), pressureRgba(0));
  near(pressureRgba(10), pressureRgba(1));
  near(pressureRgba(NaN), [0.32, 0.34, 0.38, 1]);
  near(pressureRgba(Infinity), [0.32, 0.34, 0.38, 1]);
});
