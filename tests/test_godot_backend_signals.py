#!/usr/bin/env python3
"""Static regression for Amy.gd's platform-independent backend lifecycle."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
AMY_GD = ROOT / "godot" / "amy.gd"


def function_body(source: str, name: str) -> str:
    lines = source.splitlines()
    signature = f"func {name}("
    start = next(
        index for index, line in enumerate(lines) if line.startswith(signature)
    )
    body: list[str] = []
    for line in lines[start + 1 :]:
        if line and not line.startswith(("\t", " ")):
            break
        body.append(line)
    return "\n".join(body)


class GodotBackendSignalContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = AMY_GD.read_text(encoding="utf-8")

    def test_public_signal_signatures_are_stable(self) -> None:
        self.assertEqual(self.source.count("signal backend_ready\n"), 1)
        self.assertEqual(
            self.source.count("signal backend_error(message: String)\n"), 1
        )

    def test_native_backend_reports_success_and_failure(self) -> None:
        body = function_body(self.source, "_init_native")
        self.assertIn('var message := "AmySynth GDExtension not loaded', body)
        self.assertIn("backend_error.emit(message)", body)
        self.assertIn("_started = true", body)
        self.assertIn("backend_ready.emit()", body)
        self.assertLess(
            body.index("_started = true"), body.index("backend_ready.emit()")
        )

    def test_web_backend_reports_success_and_timeout(self) -> None:
        body = function_body(self.source, "_init_web")
        self.assertIn("_started = true", body)
        self.assertIn("backend_ready.emit()", body)
        self.assertIn('var message := "AMY web module failed to load', body)
        self.assertIn("backend_error.emit(message)", body)
        self.assertLess(
            body.index("_started = true"), body.index("backend_ready.emit()")
        )


if __name__ == "__main__":
    unittest.main()
