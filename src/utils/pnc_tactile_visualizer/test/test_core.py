import copy
import json
import math
from pathlib import Path
import unittest

from pnc_tactile_visualizer.core import NamedFrame, UNKNOWN_COLOR, load_zones, response_color


class DecoderTest(unittest.TestCase):
    def test_named_decoding_and_changed_layout_do_not_reuse_old_values(self):
        frame = NamedFrame()
        frame.set_values([0.9], 1)
        self.assertTrue(math.isnan(frame.value('raa0_ch0', 1, 0.5)))
        frame.set_names(['raa8_ch5/value', 'raa0_ch0/raw_i', 'raa0_ch0/value'])
        frame.set_values([0.7, 99, 0.2], 2)
        self.assertEqual(frame.value('raa0_ch0', 2, 0.5), 0.2)
        self.assertEqual(frame.value('raa8_ch5', 2, 0.5), 0.7)
        frame.set_names(['raa0_ch0/value'])
        self.assertTrue(math.isnan(frame.value('raa0_ch0', 2, 0.5)))

    def test_unknown_stale_fault_and_recovery(self):
        frame = NamedFrame()
        frame.set_names(['raa0_ch0/value', 'raa1_ch0/value'])
        frame.set_values([0.6, math.nan], 1)
        self.assertEqual(frame.value('raa0_ch0', 1.1, 0.5), 0.6)
        self.assertTrue(math.isnan(frame.value('raa1_ch0', 1.1, 0.5)))
        self.assertTrue(math.isnan(frame.value('raa0_ch0', 1.6, 0.5)))
        frame.set_values([0.6, 0.3], 2)
        self.assertEqual(frame.value('raa1_ch0', 2, 0.5), 0.3)
        frame.set_values([0.6], 3)
        self.assertEqual(frame.state(3, 0.5), 'length_mismatch')
        self.assertTrue(math.isnan(frame.value('raa0_ch0', 3, 0.5)))

    def test_duplicate_keys_fail_closed(self):
        frame = NamedFrame()
        frame.set_names(['raa0_ch0/value', 'raa0_ch0/value'])
        frame.set_values([0.2, 0.9], 1)
        self.assertTrue(math.isnan(frame.value('raa0_ch0', 1, 0.5)))

    def test_distinct_strengths_and_invalid_are_distinct_colors(self):
        colors = [response_color(value) for value in (0.0, 0.25, 0.5, 0.75, 1.0)]
        self.assertEqual(len(set(colors)), 5)
        self.assertEqual(response_color(2), colors[-1])
        self.assertEqual(response_color(-1), colors[0])
        self.assertEqual(response_color(math.nan), UNKNOWN_COLOR)
        with self.assertRaises(ValueError):
            response_color(0, 1, 1)


class GeometryTest(unittest.TestCase):
    def test_both_hands_and_no_accidental_production_mapping(self):
        base = Path(__file__).resolve().parents[1]
        for side in ('left', 'right'):
            config = json.loads((base / 'config' / f'pnc_zones_{side}.json').read_text())
            demo = load_zones(config, side, 'demo')
            self.assertEqual(len(demo), 47)
            self.assertEqual(len({zone.channel for zone in demo}), 47)
            self.assertTrue(all(zone.triangles for zone in demo))
            self.assertTrue(all(zone.channel is None for zone in load_zones(config, side, 'unmapped')))
            with self.assertRaises(ValueError):
                load_zones(config, side, 'verified')
            invalid = copy.deepcopy(config)
            invalid['zones'][1]['demo_channel'] = invalid['zones'][0]['demo_channel']
            with self.assertRaises(ValueError):
                load_zones(invalid, side, 'demo')


if __name__ == '__main__':
    unittest.main()
