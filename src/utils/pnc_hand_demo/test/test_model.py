import math
import unittest

from pnc_hand_demo.model import (
    CHANNEL_NAMES, DemoModel, INTERFACE_KEYS, interface_values, sweep_values, validate_values,
)


class DemoModelTest(unittest.TestCase):
    def test_production_order(self):
        self.assertEqual(len(CHANNEL_NAMES), 54)
        self.assertEqual(len(set(INTERFACE_KEYS)), 162)
        self.assertEqual(INTERFACE_KEYS[:3], ('raa0_ch0/raw_i', 'raa0_ch0/raw_q', 'raa0_ch0/value'))
        self.assertEqual(INTERFACE_KEYS[-3:], ('raa8_ch5/raw_i', 'raa8_ch5/raw_q', 'raa8_ch5/value'))
        values = interface_values(range(54))
        self.assertEqual(len(values), 162)
        for index in range(54):
            self.assertEqual(values[3 * index:3 * index + 3], (float(index), 0.0, float(index)))

    def test_sweep_is_deterministic_and_periodic(self):
        first = sweep_values(3.5)
        self.assertEqual(first, sweep_values(3.5))
        self.assertEqual(first, sweep_values(21.5))
        self.assertNotEqual(first, sweep_values(4.5))
        self.assertTrue(all(0.0 <= value <= 1.0 for value in first))
        self.assertEqual(max(sweep_values(0)), 1.0)

    def test_nonfinite_means_unknown_in_every_interface(self):
        payload = [0.0] * 54
        payload[:3] = [math.nan, math.inf, -math.inf]
        values = interface_values(payload)
        self.assertTrue(all(math.isnan(value) for value in values[:9]))
        self.assertEqual(values[9:], (0.0,) * 153)

    def test_bad_input_is_atomic_and_does_not_stop_sweep(self):
        model = DemoModel()
        before = model.sample(2.5)
        for bad in (None, '0' * 54, [0.0] * 53, [0.0] * 55,
                    [False] * 54, ['0'] * 54, [None] * 54, [10 ** 1000] * 54):
            with self.subTest(bad_type=type(bad)):
                with self.assertRaises(ValueError):
                    model.set_manual_values(bad)
                self.assertEqual(model.scenario, 'sweep')
                self.assertEqual(model.sample(2.5), before)

    def test_manual_input_takes_over_and_is_not_time_dependent(self):
        model = DemoModel()
        model.set_manual_values([0.5] * 54)
        self.assertEqual(model.scenario, 'manual')
        self.assertEqual(model.sample(0), model.sample(900))
        self.assertEqual(model.sample(0)[2::3], (0.5,) * 54)
        with self.assertRaises(ValueError):
            model.set_manual_values([0.0])
        self.assertEqual(model.sample(1)[2::3], (0.5,) * 54)

    def test_one_chip_fault_and_recovery_preserve_other_contacts(self):
        model = DemoModel('manual')
        values = [0.75] * 54
        values[12:18] = [math.nan] * 6
        model.set_manual_values(values)
        sample = model.sample(1)
        self.assertTrue(all(math.isnan(value) for value in sample[36:54]))
        self.assertEqual(sample[2:36:3], (0.75,) * 12)
        self.assertEqual(sample[56::3], (0.75,) * 36)
        values[12:18] = [0.25] * 6
        model.set_manual_values(values)
        self.assertEqual(model.sample(2)[2::3], tuple(values))

    def test_relative_values_are_not_force_calibrated_or_clamped(self):
        self.assertEqual(validate_values([-2.0] * 54), (-2.0,) * 54)
        self.assertEqual(validate_values([4.0] * 54), (4.0,) * 54)

    def test_invalid_scenario_and_time(self):
        with self.assertRaises(ValueError):
            DemoModel('hardware')
        for value in (-1, math.nan, math.inf):
            with self.assertRaises(ValueError):
                sweep_values(value)


if __name__ == '__main__':
    unittest.main()
