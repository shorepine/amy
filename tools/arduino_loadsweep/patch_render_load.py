#!/usr/bin/env python3
"""Inject a 2 Hz render-load stderr print into src/i2s.c.

Ported from tulipcc's tools/amyboard_loadsweep (shorepine/tulipcc#1108).
Two variants, chosen automatically:

* Pre-#826 trees (no amy_overload_check in i2s.c): backport just the
  render-load measurement from shorepine/amy#826 as a textual patch to
  esp_fill_audio_buffer_task — time the render work (busy) vs the i2s DMA
  write / update-sync wait (blocked), keep a 0.05-per-block smoothed
  load = busy/(busy+blocked), and fprintf it to stderr twice per second:

      RENDER_LOAD ms=<board_ms> load=<0..1>

  The anchors were verified stable from 2026-03-30 (v1.2.4) through the
  #826 merge — i2s.c's task body changed only once in that window and not
  in this function.

* Post-#826 trees (amy_overload_check present): AMY already keeps the same
  0.05-smoothed load in amy_global.render_load, so just print that at 2 Hz
  and pin config.overload_threshold to 0 so the failsafe can't reset the
  synth mid-measurement.  Numbers are directly comparable to the backport
  (same smoothing, same busy/period definition).

Idempotent; fails loudly (exit != 0) if no anchor matches.

Usage: patch_render_load.py <path-to-amy>/src/i2s.c
"""
import re
import sys

MARK = "/* rl-patch */"
PRINT_US = 500000  # 2 Hz

DECLS = """
%s
#include "esp_timer.h"
#include <stdio.h>
static float _rl_load = 0.0f;
static int64_t _rl_last_print = 0;
""" % MARK

POST826 = """
        %s
        { int64_t _rl_now = esp_timer_get_time();
          if (_rl_now - _rl_last_print > %d) {
              _rl_last_print = _rl_now;
              amy_global.config.overload_threshold = 0;  /* no failsafe during measurement */
              fprintf(stderr, "RENDER_LOAD ms=%%lu load=%%.4f\\n",
                      (unsigned long)(_rl_now/1000), amy_global.render_load);
              fflush(stderr);
          } }
"""


def patch_post826(src):
    """Tree already measures load (amy#826): print amy_global.render_load."""
    anchor = re.search(
        r"(?m)^( *)amy_overload_check\(busy_us, busy_us \+ blocked_us\);", src)
    if not anchor:
        sys.exit("[patch] FAILED: post-826 tree but no "
                 "'amy_overload_check(busy_us, busy_us + blocked_us);' anchor")
    decls = ('\n%s\n#include "esp_timer.h"\n#include <stdio.h>\n'
             "static int64_t _rl_last_print = 0;\n" % MARK)
    src, n = re.subn(r"(?m)^(// Make AMY's FABT run forever.*\n)?"
                     r"^void esp_fill_audio_buffer_task\(\) \{",
                     decls + "void esp_fill_audio_buffer_task() {", src, count=1)
    if n != 1:
        sys.exit("[patch] FAILED: post-826: function head anchor not found")
    inject = POST826 % (anchor.group(0).strip(), PRINT_US)
    src = src.replace(anchor.group(0), inject.rstrip(), 1)
    return src, ["post-826 render_load print"]


def patch_pre826(src):
    """Backport the measurement (identical to tulipcc#1108's patch)."""
    subs = []

    def sub(pattern, repl, label):
        nonlocal src

        def expand(m):
            return re.sub(r"\\(\d)", lambda g: m.group(int(g.group(1))) or "", repl)

        new, n = re.subn(pattern, expand, src, count=1)
        if n != 1:
            sys.exit(f"[patch] FAILED: anchor not found: {label}")
        src = new
        subs.append(label)

    # 0) statics + includes before the task function
    sub(r"(?m)^(// Make AMY's FABT run forever.*\n)?^void esp_fill_audio_buffer_task\(\) \{",
        DECLS + "void esp_fill_audio_buffer_task() {",
        "function head")

    # 1) locals at top of the while loop + busy timer start after the
    #    (blocked-time-tracked) audio-in read
    sub(r"(void esp_fill_audio_buffer_task\(\) \{\n    while\(1\) \{\n)",
        "\\1        int64_t _rl_t; uint32_t _rl_busy = 0, _rl_blocked = 0;\n",
        "loop head")

    sub(r"( *)if\(AMY_HAS_I2S && AMY_HAS_AUDIO_IN\) \{\n( *)esp_read_i2s_input\(\);\n(\s*)\}",
        "\\1if(AMY_HAS_I2S && AMY_HAS_AUDIO_IN) {\n"
        "\\2_rl_t = esp_timer_get_time();\n"
        "\\2esp_read_i2s_input();\n"
        "\\2_rl_blocked += (uint32_t)(esp_timer_get_time() - _rl_t);\n"
        "\\3}\n"
        "        _rl_t = esp_timer_get_time();",
        "audio-in block")

    # 2) busy time ends after amy_fill_buffer
    sub(r"( *)(output_sample_type \*block = amy_fill_buffer\(\);)",
        "\\1\\2\n\\1_rl_busy = (uint32_t)(esp_timer_get_time() - _rl_t);",
        "fill buffer")

    # 3) blocked time around the i2s write / sync wait, then smooth + 2 Hz print
    sub(r"( *)(if \(AMY_HAS_I2S\) \{\n"
        r" *amy_i2s_write\(\(uint8_t \*\)block, AMY_BLOCK_SIZE \* AMY_NCHANS \* sizeof\(int16_t\)\);\n"
        r" *\} else \{\n"
        r" *// Wait for update sync\.\n"
        r" *ulTaskNotifyTake\(pdTRUE, portMAX_DELAY\);.*\n"
        r" *\})\n",
        "\\1_rl_t = esp_timer_get_time();\n"
        "\\1\\2\n"
        "\\1_rl_blocked += (uint32_t)(esp_timer_get_time() - _rl_t);\n"
        "\\1if (_rl_busy + _rl_blocked > 0)\n"
        "\\1    _rl_load += 0.05f * ((float)_rl_busy / (float)(_rl_busy + _rl_blocked) - _rl_load);\n"
        "\\1{ int64_t _rl_now = esp_timer_get_time();\n"
        "\\1  if (_rl_now - _rl_last_print > %d) {\n"
        "\\1      _rl_last_print = _rl_now;\n"
        "\\1      fprintf(stderr, \"RENDER_LOAD ms=%%lu load=%%.4f\\n\", (unsigned long)(_rl_now/1000), _rl_load);\n"
        "\\1      fflush(stderr);\n"
        "\\1  } }\n" % PRINT_US,
        "i2s write block")

    return src, subs


def main(path):
    src = open(path).read()
    if MARK in src:
        print(f"[patch] {path} already patched")
        return 0
    if "amy_overload_check(busy_us" in src:
        src, subs = patch_post826(src)
    else:
        src, subs = patch_pre826(src)
    open(path, "w").write(src)
    print(f"[patch] OK: {', '.join(subs)} -> {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
