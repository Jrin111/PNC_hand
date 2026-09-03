"""Deterministic tactile data model, deliberately independent of ROS."""

import math
from numbers import Real


CHIP_COUNT = 9
CHANNELS_PER_CHIP = 6
CHANNEL_COUNT = CHIP_COUNT * CHANNELS_PER_CHIP
CHANNEL_NAMES = tuple(
    f'raa{chip}_ch{channel}'
    for chip in range(CHIP_COUNT)
    for channel in range(CHANNELS_PER_CHIP)
)
INTERFACES = ('raw_i', 'raw_q', 'value')
INTERFACE_KEYS = tuple(
    f'{name}/{interface}' for name in CHANNEL_NAMES for interface in INTERFACES
)
SWEEP_PERIOD_SECONDS = 18.0


def validate_values(payload):
    """Return 54 floats, converting all nonfinite samples to unknown (NaN).

    The array is validated in full before a caller changes its current state.
    Signed relative values are allowed; these are not calibrated forces.
    """
    if isinstance(payload, (str, bytes)):
        raise ValueError('expected an array of 54 numeric values')
    try:
        values = list(payload)
    except TypeError as exc:
        raise ValueError('expected an array of 54 numeric values') from exc
    if len(values) != CHANNEL_COUNT:
        raise ValueError(f'expected 54 values, received {len(values)}')
    normalized = []
    for index, value in enumerate(values):
        if isinstance(value, bool) or not isinstance(value, Real):
            raise ValueError(f'channel {index} must be numeric')
        try:
            value = float(value)
        except (OverflowError, ValueError) as exc:
            raise ValueError(f'channel {index} is outside float64 range') from exc
        normalized.append(value if math.isfinite(value) else math.nan)
    return tuple(normalized)


def interface_values(relative_values):
    """Match production's [raw_i, raw_q, value] ordering for every channel.

    Demo raw_i equals the relative input and raw_q is zero. They are synthetic
    interface placeholders, not emulated RAA ADC readings or physical forces.
    Unknown channels have NaN in all three interfaces, never a false release.
    """
    values = validate_values(relative_values)
    return tuple(
        component
        for value in values
        for component in ((value, 0.0, value) if math.isfinite(value)
                          else (math.nan, math.nan, math.nan))
    )


def sweep_values(elapsed_seconds):
    """One moving contact over stable electrical channel order, every 18 s.

    This is a display scenario; adjacent channel numbers do not assert physical
    adjacency on the hand. The spatial mapping is explicitly a demo profile.
    """
    if not math.isfinite(elapsed_seconds) or elapsed_seconds < 0:
        raise ValueError('elapsed time must be finite and nonnegative')
    center = (elapsed_seconds % SWEEP_PERIOD_SECONDS) / SWEEP_PERIOD_SECONDS * CHANNEL_COUNT
    values = []
    for index in range(CHANNEL_COUNT):
        distance = abs(index - center)
        distance = min(distance, CHANNEL_COUNT - distance)
        values.append(max(0.0, 1.0 - distance / 2.0))
    return tuple(values)


class DemoModel:
    """A valid manual command takes over a sweep until the source is restarted."""

    def __init__(self, scenario='sweep'):
        if scenario not in ('sweep', 'manual'):
            raise ValueError('scenario must be sweep or manual')
        self.scenario = scenario
        self._manual_values = (0.0,) * CHANNEL_COUNT

    def set_manual_values(self, payload):
        values = validate_values(payload)
        self._manual_values = values
        self.scenario = 'manual'

    def relative_values(self, elapsed_seconds):
        if self.scenario == 'manual':
            return self._manual_values
        return sweep_values(elapsed_seconds)

    def sample(self, elapsed_seconds):
        return interface_values(self.relative_values(elapsed_seconds))
