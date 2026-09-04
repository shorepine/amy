"""Small, audio-independent checks for the reusable-sequence Python API."""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))

import amy


def expect_error(fragment, fn):
    try:
        fn()
    except ValueError as exc:
        assert fragment in str(exc), str(exc)
    else:
        raise AssertionError("expected ValueError containing %r" % fragment)


def main():
    assert amy.message(ticks=(0, 0, 7), synth=1, note=60, vel=1) \
        == "H0,0,7n60l1i1Z"
    assert amy.message(sequence_control=(7, amy.SEQUENCE_CONTROL_START, 48)) \
        == "HC7,1,48Z"
    assert amy.message(sequence_control=("%v", "%v", "%v", "%v")) \
        == "HC%v,%v,%v,%vZ"
    assert amy.message(ticks=(0, 48, 3),
                       sequence_control=(7, amy.SEQUENCE_CONTROL_START, 1)) \
        == "H0,48,3HC7,1,1Z"
    assert amy.message(sequence=7, action="start") == "HC7,1,0Z"
    assert amy.message(sequence=7, action="stop", alignment_period=48) \
        == "HC7,0,48Z"
    assert amy.message(sequence=7, action="gate", duration=24,
                       alignment_period=1) == "HC7,2,24,1Z"
    assert amy.message(ticks=(0, 48, 3), sequence=7, action="start",
                       alignment_period=1) == "H0,48,3HC7,1,1Z"
    assert amy.message(sequence_reset=7) == "HR7Z"
    assert amy.message(ticks=(1, 4, 2), synth=1, note=60, vel=1) \
        == "H1,4,2n60l1i1Z"
    assert amy.message(ticks=",24,2", osc=1) == "H,24,2v1Z"
    assert amy.message(ticks=(None, 24, 2), osc=1) == "H,24,2v1Z"
    assert amy.message(ticks=(4, 4), osc=1) == "H4,4v1Z"

    sent = []
    old_override = amy.override_send
    amy.override_send = sent.append
    try:
        amy.define_sequence(7, [
            {"ticks": (0,), "synth": 1, "note": 60, "vel": 1},
            {"ticks": (3, 8), "synth": 1, "note": 60, "vel": 0},
        ])
    finally:
        amy.override_send = old_override
    assert sent == [
        "HR7Z",
        "H0,0,7n60l1i1Z",
        "H3,8,7n60l0i1Z",
    ]

    expect_error("standalone", lambda: amy.message(sequence_reset=2, synth=1))
    expect_error("tick", lambda: amy.message(ticks=(1.5,), osc=1))
    expect_error("period", lambda: amy.message(ticks=(4, 4, 2), osc=1))
    expect_error("tag", lambda: amy.message(ticks=(0, 4, True), osc=1))
    expect_error("only be combined", lambda: amy.message(
        sequence_control=(2, 1), synth=1))
    expect_error("only be combined", lambda: amy.message(
        ticks=(0,), sequence_control=(2, 1), synth=1))
    expect_error("start/stop", lambda: amy.message(sequence_control=(2, 1, 3, 4)))
    expect_error("duration", lambda: amy.message(sequence_control=(2, 2)))
    expect_error("action", lambda: amy.message(sequence_control=(2, 99)))
    expect_error("action", lambda: amy.message(sequence_control=(2, -0.1)))
    expect_error("integer", lambda: amy.message(sequence_control=(2, 0.625)))
    expect_error("integer", lambda: amy.message(sequence_control=(2, True)))
    expect_error("tag", lambda: amy.message(sequence_control=(1.5, 1)))
    expect_error("alignment", lambda: amy.message(sequence_control=(2, 1, 1.5)))
    expect_error("uint32", lambda: amy.message(
        sequence_control=(2, 2, 1 << 32)))
    expect_error("2147483647", lambda: amy.message(
        sequence_control=(2, 2, 1 << 31)))
    expect_error("tag", lambda: amy.message(sequence_reset=1.5))
    expect_error("tag", lambda: amy.message(sequence=True, action="start"))
    expect_error("tag", lambda: amy.message(sequence=1.5, action="start"))
    expect_error("duration", lambda: amy.message(
        sequence=2, action="gate", duration=1.5))
    expect_error("alignment", lambda: amy.message(
        sequence=2, action="start", alignment_period=1.5))
    expect_error("2147483647", lambda: amy.message(
        sequence=2, action="start", alignment_period=1 << 31))
    expect_error("needs action", lambda: amy.message(sequence=2))
    expect_error("can only be combined", lambda: amy.message(
        sequence=2, action="start", synth=1))
    expect_error("only valid", lambda: amy.message(alignment_period=4, synth=1))
    expect_error("only valid", lambda: amy.message(action="start", synth=1))
    expect_error("start", lambda: amy.message(sequence=2, action=True))
    expect_error("start", lambda: amy.message(sequence=2, action=1))
    expect_error("duration", lambda: amy.message(sequence=2, action="gate"))
    expect_error("only valid", lambda: amy.message(
        sequence=2, action="start", duration=1))
    expect_error("non-negative", lambda: amy.message(
        sequence=2, action="gate", duration=-1))
    expect_error("needs a ticks", lambda: amy.define_sequence(2, [{"synth": 1}]))
    expect_error("needs an AMY payload", lambda: amy.define_sequence(
        2, [{"ticks": (0,)}]))
    expect_error("tick", lambda: amy.define_sequence(
        2, [{"ticks": (1.5,), "osc": 1}]))
    expect_error("period", lambda: amy.define_sequence(
        2, [{"ticks": (1, 1 << 32), "osc": 1}]))


if __name__ == "__main__":
    main()
