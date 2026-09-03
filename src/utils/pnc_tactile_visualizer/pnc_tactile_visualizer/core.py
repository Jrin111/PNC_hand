"""Strict named-channel decoding and physical-zone color mapping.

Values are relative PNC response, NOT Newtons. NaN denotes unavailable data.
Message freshness alone cannot prove device health, particularly with older plugins.
"""
from __future__ import annotations

from dataclasses import dataclass
import math

CHANNELS = tuple(f'raa{chip}_ch{channel}' for chip in range(9) for channel in range(6))
UNKNOWN_COLOR = (0.32, 0.34, 0.38, 1.0)
COLOR_STOPS = (
    (0.0, (0.06, 0.18, 0.55)),
    (0.25, (0.0, 0.62, 0.88)),
    (0.5, (0.12, 0.82, 0.42)),
    (0.75, (1.0, 0.79, 0.05)),
    (1.0, (0.94, 0.12, 0.08)),
)


def finite_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def response_color(value, minimum=0.0, maximum=1.0):
    if not finite_number(minimum) or not finite_number(maximum) or maximum <= minimum:
        raise ValueError('color_max must be finite and greater than color_min')
    if not finite_number(value):
        return UNKNOWN_COLOR
    fraction = min(1.0, max(0.0, (value - minimum) / (maximum - minimum)))
    for (lo, start), (hi, end) in zip(COLOR_STOPS, COLOR_STOPS[1:]):
        if fraction <= hi:
            weight = (fraction - lo) / (hi - lo)
            return tuple(a + weight * (b - a) for a, b in zip(start, end)) + (1.0,)
    return COLOR_STOPS[-1][1] + (1.0,)


class NamedFrame:
    def __init__(self):
        self.keys = ()
        self.indices = {}
        self.values = {}
        self.received_at = None
        self.error = 'waiting_for_names'

    def set_names(self, keys):
        keys = tuple(keys)
        if not keys or not all(isinstance(key, str) for key in keys) or len(set(keys)) != len(keys):
            self.keys, self.indices, self.values = (), {}, {}
            self.received_at = None
            self.error = 'invalid_names'
            return
        if keys != self.keys:
            self.values, self.received_at = {}, None
            self.error = 'waiting_for_values'
        self.keys = keys
        self.indices = {channel: keys.index(channel + '/value') for channel in CHANNELS
                        if channel + '/value' in keys}

    def set_values(self, values, now):
        self.values = {}
        self.received_at = None
        if not self.keys:
            self.error = 'waiting_for_names'
            return
        if len(values) != len(self.keys):
            self.error = 'length_mismatch'
            return
        self.values = {name: float(values[index]) if finite_number(values[index]) else math.nan
                       for name, index in self.indices.items()}
        self.received_at = now
        self.error = ''

    def state(self, now, timeout):
        if self.error:
            return self.error
        if self.received_at is None or now - self.received_at > timeout or now < self.received_at:
            return 'stale'
        return 'receiving'

    def value(self, channel, now, timeout):
        if self.state(now, timeout) != 'receiving':
            return math.nan
        return self.values.get(channel, math.nan)


@dataclass(frozen=True)
class Zone:
    id: str
    label: str
    frame_id: str
    polygon: tuple
    channel: str | None
    gain: float = 1.0
    offset: float = 0.0

    @property
    def triangles(self):
        # The configuration contains convex surface patches, preserving winding.
        return tuple(point for index in range(1, len(self.polygon) - 1)
                     for point in (self.polygon[0], self.polygon[index], self.polygon[index + 1]))


def load_zones(config, side, profile):
    if config.get('schema_version') != 1 or config.get('hand_side') != side:
        raise ValueError('geometry schema or hand_side mismatch')
    if profile not in ('unmapped', 'demo', 'verified'):
        raise ValueError('mapping_profile must be unmapped, demo, or verified')
    if profile == 'verified' and config.get('mapping_verified') is not True:
        raise ValueError('verified profile requires a physically verified mapping file')
    raw_zones = config.get('zones', [])
    if len(raw_zones) != 47:
        raise ValueError('a hand must contain exactly 47 physical zones')
    zones, ids, channels = [], set(), set()
    for entry in raw_zones:
        zone_id = entry['id']
        if not isinstance(zone_id, str) or not zone_id or zone_id in ids:
            raise ValueError('zone IDs must be unique nonempty strings')
        ids.add(zone_id)
        frame = entry['frame_id']
        if not isinstance(frame, str) or not frame or frame.startswith('/'):
            raise ValueError('frame_id must be a nonempty relative TF frame')
        polygon = entry['polygon']
        if not isinstance(polygon, list) or not 3 <= len(polygon) <= 12:
            raise ValueError('each patch needs 3..12 convex polygon vertices')
        if any(not isinstance(point, list) or len(point) != 3 or
               not all(finite_number(v) for v in point) for point in polygon):
            raise ValueError('patch coordinates must be finite XYZ metres')
        channel = (entry.get('demo_channel') if profile == 'demo' else
                   entry.get('channel') if profile == 'verified' else None)
        if profile != 'unmapped':
            if channel not in CHANNELS or channel in channels:
                raise ValueError('every mapped zone needs a distinct valid electrical channel')
            channels.add(channel)
        gain, offset = entry.get('gain', 1.0), entry.get('offset', 0.0)
        if not finite_number(gain) or gain == 0 or not finite_number(offset):
            raise ValueError('gain must be finite and nonzero; offset must be finite')
        zones.append(Zone(zone_id, entry.get('label', zone_id), frame,
                          tuple(tuple(float(v) for v in point) for point in polygon),
                          channel, float(gain), float(offset)))
    return zones
