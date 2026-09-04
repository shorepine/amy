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
    assert amy.message(sequence_event=(7, 0, 0), synth=1, note=60, vel=1) \
        == "HA7,0,0n60l1i1Z"
    assert amy.message(sequence_control=(7, amy.SEQUENCE_CONTROL_START, 48)) \
        == "HC7,1,48Z"
    assert amy.message(ticks=(0, 48, 3),
                       sequence_control=(7, amy.SEQUENCE_CONTROL_START, 1)) \
        == "H0,48,3HC7,1,1Z"
    assert amy.message(sequence_reset=7) == "HR7Z"
    assert amy.message(ticks=(1, 4, 2), synth=1, note=60, vel=1) \
        == "H1,4,2n60l1i1Z"

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
        "HA7,0,0n60l1i1Z",
        "HA7,3,8n60l0i1Z",
    ]

    expect_error("only one", lambda: amy.message(
        ticks=(0, 4, 1), sequence_event=(2, 0, 0), synth=1))
    expect_error("standalone", lambda: amy.message(sequence_reset=2, synth=1))
    expect_error("only be combined", lambda: amy.message(
        sequence_control=(2, 1), synth=1))
    expect_error("needs a ticks", lambda: amy.define_sequence(2, [{"synth": 1}]))
    expect_error("needs an AMY payload", lambda: amy.define_sequence(
        2, [{"ticks": (0,)}]))


if __name__ == "__main__":
    main()
