import unittest

import amy
import c_amy


class StateDumpTest(unittest.TestCase):
    def setUp(self):
        c_amy.stop()
        c_amy.start(0)

    def apply(self, **kwargs):
        amy.send(**kwargs)
        # Wire messages become engine state while AMY renders its next block.
        c_amy.render_to_list()

    def test_dump_contains_active_bus_state(self):
        self.apply(synth=17, num_voices=1, oscs_per_voice=1, bus=2)
        self.apply(synth=18, num_voices=1, oscs_per_voice=1, bus=3)

        self.apply(bus=0, volume=0.25, eq=[-3, 0, 2])
        self.apply(bus=1, volume=0.5, reverb=[0.2, 0.6, 0.4, 3500])
        self.apply(bus=2, volume=0.75, chorus=[0.3, 120, 1.5, 0.4])
        self.apply(bus=3, volume=0.9, echo=[0.35, 180, 600, 0.45, 0.1])

        lines = c_amy.dump_state().splitlines()

        for bus in range(4):
            self.assertEqual(
                1,
                sum(line.startswith("y%d" % bus) for line in lines),
                "expected exactly one explicit state line for bus %d" % bus,
            )
            bus_line = next(line for line in lines if line.startswith("y%d" % bus))
            self.assertEqual(1, bus_line.count("y%d" % bus), "bus wirecode must be serialized once")

        bus_lines = {
            bus: next(line for line in lines if line.startswith("y%d" % bus))
            for bus in range(4)
        }
        self.assertIn("V0.250", bus_lines[0])
        self.assertIn("x-3.000,0.000,2.000", bus_lines[0])
        self.assertIn("V0.500", bus_lines[1])
        self.assertIn("h0.200,0.600,0.400,3500.000", bus_lines[1])
        self.assertIn("V0.750", bus_lines[2])
        self.assertIn("k0.300,120.000,1.500,0.400", bus_lines[2])
        self.assertIn("V0.900", bus_lines[3])
        # max_delay is rounded to AMY's power-of-two delay-line capacity. The resulting default-sized
        # capacity is intentionally omitted from replay wirecode, hence the empty third field.
        self.assertIn("M0.350,180.000,,0.450,0.100", bus_lines[3])

    def test_dump_skips_inactive_buses(self):
        # Routing a synth to bus 1 activates buses 0..1; buses 2..3 stay inactive.
        # Replaying state for an inactive bus would activate it, so it must not be dumped.
        self.apply(synth=17, num_voices=1, oscs_per_voice=1, bus=1)

        lines = c_amy.dump_state().splitlines()

        for bus in (0, 1):
            self.assertEqual(
                1,
                sum(line.startswith("y%d" % bus) for line in lines),
                "expected exactly one state line for active bus %d" % bus,
            )
        for bus in (2, 3):
            self.assertEqual(
                0,
                sum(line.startswith("y%d" % bus) for line in lines),
                "inactive bus %d must not appear in the dump" % bus,
            )

    def test_dump_replays_bus_state(self):
        self.apply(synth=17, num_voices=1, oscs_per_voice=1, bus=2)
        self.apply(synth=18, num_voices=1, oscs_per_voice=1, bus=3)
        self.apply(
            bus=0,
            volume=0.4,
            eq=[-2, 1, 3],
            reverb=[0.1, 0.5, 0.2, 2500],
            chorus=[0.15, 72, 0.8, 0.2],
            echo=[0.2, 120, 500, 0.25, 0.05],
        )
        self.apply(
            bus=1,
            volume=0.55,
            eq=[-1, 2, 4],
            reverb=[0.2, 0.6, 0.3, 3500],
            chorus=[0.25, 96, 1.2, 0.3],
            echo=[0.3, 160, 600, 0.35, 0.1],
        )
        self.apply(
            bus=2,
            volume=0.7,
            eq=[0, 3, 5],
            reverb=[0.3, 0.7, 0.4, 4500],
            chorus=[0.35, 120, 1.6, 0.4],
            echo=[0.4, 200, 700, 0.45, 0.15],
        )
        self.apply(
            bus=3,
            volume=0.85,
            eq=[1, 4, 6],
            reverb=[0.4, 0.8, 0.5, 5500],
            chorus=[0.45, 144, 2.0, 0.5],
            echo=[0.5, 240, 800, 0.55, 0.2],
        )
        original = c_amy.dump_state()

        c_amy.stop()
        c_amy.start(0)
        c_amy.send_wire(original.replace("\n", ""))
        # Drain every replay event, not merely the first block.
        for _ in range(8):
            c_amy.render_to_list()

        # Equality also proves highest_bus is restored: a lower value would drop bus lines
        # from the second dump.
        self.assertEqual(original, c_amy.dump_state())

    def test_public_dump_state_matches_c_module(self):
        self.apply(synth=17, num_voices=1, oscs_per_voice=1, bus=2)
        self.assertEqual(c_amy.dump_state(), amy.dump_state())


if __name__ == "__main__":
    unittest.main()
