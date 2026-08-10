#!/usr/bin/env python3
"""Regenerate the _KW_MAP / _KW_PRIORITY block in godot/amy.gd.

The send() keyword -> wire-code mapping is defined once, in amy/__init__.py's
_KW_MAP_LIST (the Python source of truth). This script mirrors it into the
GDScript wrapper so the Godot API can never silently drift -- the same role
gen_amy_js_api.py plays for the browser build.

Only the region between the BEGIN/END markers in godot/amy.gd is rewritten;
the rest of the file is hand-maintained. Run via `make godot-api`. It runs on
release (release.yml) and is checked in CI (c-cpp.yml), so a kwarg added to
_KW_MAP_LIST without regenerating fails the build.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AMY_INIT = ROOT / "amy" / "__init__.py"
GODOT_GD = ROOT / "godot" / "amy.gd"

BEGIN = "# BEGIN GENERATED - scripts/gen_amy_gd_api.py"
END = "# END GENERATED"

# Reuse the exact same _KW_MAP_LIST parser the JS generator uses, so the two
# language bindings can never disagree about how the Python source is read.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from gen_amy_js_api import (  # noqa: E402
    extract_kw_map_list, extract_coef_fields, extract_coef_aliases,
)


def render_block(kw_map_list, coef_fields, coef_aliases):
    """Render the GDScript _KW_MAP, _KW_PRIORITY and coef-name constants."""
    # Pad the key column so the value arrays line up, matching the file's style.
    width = max(len(name) for name, _ in kw_map_list) + len('"":')
    map_lines = []
    prio_lines = []
    for i, (name, code) in enumerate(kw_map_list):
        wire, type_code = code[:-1], code[-1]
        key = '"%s":' % name
        map_lines.append('\t%-*s ["%s", "%s"],' % (width, key, wire, type_code))
        prio_lines.append('\t"%s": %d,' % (name, i))
    coef_fields_gd = ", ".join('"%s"' % f for f in coef_fields)
    coef_aliases_gd = ", ".join('"%s": "%s"' % kv for kv in coef_aliases.items())
    return "\n".join(
        ["var _KW_MAP: Dictionary = {"]
        + map_lines
        + ["}", "", "var _KW_PRIORITY: Dictionary = {"]
        + prio_lines
        + ["}", "",
           "## The control coefficient inputs, in wire order.  Prefer naming these in a",
           "## Dictionary over writing a positional Array: anything past the modulators",
           "## shifts position whenever a new control input is added.",
           "const COEF_FIELDS: PackedStringArray = [%s]" % coef_fields_gd,
           "",
           "## Superseded coef names, kept working: {old: new}.",
           "const COEF_ALIASES: Dictionary = {%s}" % coef_aliases_gd]
    )


def main():
    source = AMY_INIT.read_text()
    kw_map_list = extract_kw_map_list(source)
    block = render_block(kw_map_list, extract_coef_fields(source),
                         extract_coef_aliases(source))

    lines = GODOT_GD.read_text().split("\n")
    try:
        b = next(i for i, l in enumerate(lines) if l.strip() == BEGIN)
        e = next(i for i, l in enumerate(lines) if l.strip() == END)
    except StopIteration:
        raise SystemExit("Could not find BEGIN/END GENERATED markers in %s" % GODOT_GD)
    if e <= b:
        raise SystemExit("END marker precedes BEGIN marker in %s" % GODOT_GD)

    new_text = "\n".join(lines[: b + 1] + block.split("\n") + lines[e:])
    GODOT_GD.write_text(new_text)
    print("Updated %s (%d parameters)" % (GODOT_GD.relative_to(ROOT), len(kw_map_list)))


if __name__ == "__main__":
    main()
