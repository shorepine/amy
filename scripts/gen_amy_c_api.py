#!/usr/bin/env python3
"""Generate the cross-platform AMY C API bindings from one function table.

The API table below is the single source of truth for every AMY C function
exposed to host languages. From it we generate, for each platform:

  CPython (src/pyamy.c):
    src/amy_c_api_py.inc        wrapper functions
    src/amy_c_api_py_table.inc  PyMethodDef rows (textually included in the table)

  MicroPython, linked AMY (tulipcc tulip/shared/modtulip.c):
    src/amy_c_api_mp.inc        wrapper functions + MP_DEFINE_CONST_FUN objects
    src/amy_c_api_mp_table.inc  MP_ROM_QSTR rows (textually included in the table)

  Web / emscripten (tulipcc spss.js, JS passthrough since MP doesn't link AMY):
    src/amy_c_api.generated.js  amy_c_api_bind(module) -> {fn...} plus the
                                MicroPython install snippet (AMY_C_API_PY_INSTALL)
    src/amy_c_api_exports.mk    EXPORTED_FUNCTIONS fragment for the web Makefile

  Godot (godot/src/amy_gdextension.{h,cpp}):
    godot/src/amy_c_api_gd_methods.inc  inline member functions (class body)
    godot/src/amy_c_api_gd_bind.inc     ClassDB::bind_method rows

  Python dispatch (amy/__init__.py):
    the marker block between "# BEGIN GENERATED - scripts/gen_amy_c_api.py"
    and "# END GENERATED" is rewritten with per-function backend resolution
    (_<name>) and public amy.<name> entry points.

All outputs are committed; run this script after editing the table and commit
the results (make c-api). `--check` exits nonzero if any output is stale.

The goal: amy.<py_name>(...) behaves identically on CPython, every
linked-MicroPython target, both web apps, and (subset) Godot.
"""
import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# ----------------------------------------------------------------------------
# The API table.
#
# Each entry:
#   py         python-visible name: amy.<py>  (and c_amy.<py> on CPython)
#   c          the AMY C function it binds
#   mp         name in the MicroPython `tulip` module (default: "amy_"+py).
#              These stay as the C-level surface / legacy redirect targets;
#              the canonical entry point is amy.<py>.
#   args       [(name, type, default)] with type in u8|u16|i32|u32|f32|bool|str
#              default is a C-literal string or None (required arg). Optional
#              args must come last.
#   ret        void|u32|i32|f32  (ignored for special kinds)
#   kind       'scalar' (default; also covers str args),
#              'string_generator'  (yield_synth_commands-style void* iterator),
#              'string_out_malloc' (returns malloc'd char* + int out-len),
#              'bytes_out'         (fills caller's 1024-byte buffer, returns n)
#   platforms  subset of {'py','mp','web','gd'}
#   py_public  if False, only the _<py> backend is generated in amy/__init__.py
#              (a hand-written public wrapper adds value on top, e.g.
#              amy.get_synth_commands's patch_num/dest_synth handling).
#   doc        one-line docstring / method-table doc
# ----------------------------------------------------------------------------

API = [
    dict(py='send_wire', c='amy_add_message', mp='amy_send',
         args=[('message', 'str', None)], ret='void',
         doc='Send a wire-protocol message to AMY',
         platforms={'py', 'mp', 'web'}),
    dict(py='send_wire_from_sysex', c='amy_add_message_from_sysex',
         args=[('message', 'str', None)], ret='void',
         doc='Send a wire message as if from sysex (file-transfer routing applies)',
         platforms={'py'}),
    dict(py='ticks_ms', c='amy_sysclock',
         args=[], ret='u32',
         doc='Read the AMY millisecond clock',
         platforms={'py', 'mp', 'web'}),
    dict(py='reset_sysclock', c='amy_reset_sysclock',
         args=[], ret='void',
         doc='Reset the AMY millisecond clock to zero',
         platforms={'py', 'mp', 'web', 'gd'}),
    dict(py='render_load', c='amy_get_render_load',
         args=[], ret='f32',
         doc='Smoothed fraction of real time AMY spends rendering (0..1)',
         platforms={'py', 'mp', 'web', 'gd'}),
    dict(py='set_render_load_threshold', c='amy_set_render_load_threshold',
         args=[('threshold', 'f32', None)], ret='void',
         doc='Set the render-load fraction that trips the overload failsafe (0 disables)',
         platforms={'py', 'mp', 'web', 'gd'}),
    dict(py='bleep', c='amy_bleep',
         args=[('start', 'u32', '0')], ret='void',
         doc='Play the startup bleep',
         platforms={'py', 'mp', 'web', 'gd'}),
    dict(py='sequencer_ticks', c='sequencer_ticks',
         args=[], ret='u32',
         doc='Read the sequencer tick count',
         platforms={'py', 'mp', 'web', 'gd'}),
    dict(py='process_single_midi_byte', c='amy_process_single_midi_byte',
         args=[('byte', 'u8', None), ('from_web_or_usb', 'u8', '1')], ret='void',
         doc="Feed one MIDI byte to AMY's stream parser",
         platforms={'py', 'mp', 'web'}),
    dict(py='set_cv_from_osc', c='set_cv_from_osc',
         args=[('cv_channel', 'i32', None), ('osc', 'i32', None)], ret='void',
         doc='Feed external CV input from a mod osc (testing support)',
         platforms={'py', 'mp', 'web'}),
    dict(py='get_synth_commands', c='yield_synth_commands', mp='amy_get_synth_commands',
         kind='string_generator',
         args=[('synth', 'u8', None), ('include_fx', 'bool', '1')],
         py_public=False,
         doc='Read the wire commands that reconstruct synth state (list of str)',
         platforms={'py', 'mp', 'web'}),
    dict(py='dump_state', c='amy_dump_state_to_string',
         kind='string_out_malloc', args=[],
         doc='Read the complete replayable AMY state as a wire-command string',
         platforms={'py', 'mp', 'web', 'gd'}),
    dict(py='get_output_buffer', c='amy_get_output_buffer',
         kind='bytes_out', args=[],
         doc='Read the most recent rendered audio block as bytes (None if none ready)',
         platforms={'py', 'mp', 'web'}),
    dict(py='get_input_buffer', c='amy_get_input_buffer',
         kind='bytes_out', args=[],
         doc='Read the most recent captured input audio block as bytes (None if none ready)',
         platforms={'py', 'mp', 'web'}),
]

GENERATED_NOTE = 'GENERATED by scripts/gen_amy_c_api.py -- do not edit; edit the table there'
BYTES_OUT_BUF = 1024   # amy_get_{output,input}_buffer contract: <=1024 bytes
MAX_MSG = 'MAX_MESSAGE_LEN'

C_ARG_TYPE = {'u8': 'uint8_t', 'u16': 'uint16_t', 'i32': 'int32_t',
              'u32': 'uint32_t', 'f32': 'float', 'bool': 'bool', 'str': 'char *'}


def mp_name(e):
    return e.get('mp', 'amy_' + e['py'])


def norm(e):
    e = dict(e)
    e.setdefault('kind', 'scalar')
    e.setdefault('py_public', True)
    return e


API = [norm(e) for e in API]


def c_call(e, argnames):
    return '%s(%s)' % (e['c'], ', '.join(argnames))


# ---------------------------------------------------------------- CPython ---

def gen_py_wrapper(e):
    fn = 'amy_capi_py_%s' % e['py']
    body = []
    body.append('static PyObject * %s(PyObject *self, PyObject *args) {' % fn)
    body.append('    (void)self;')
    if e['kind'] == 'scalar':
        fmt_req, fmt_opt, decls, parse_args, callargs = '', '', [], [], []
        for name, t, default in e['args']:
            if t == 'str':
                decls.append('    char *%s = NULL;' % name)
                fmt = 's'
                callargs.append(name)
            elif t == 'f32':
                decls.append('    float %s = %s;' % (name, default or '0'))
                fmt = 'f'
                callargs.append(name)
            elif t == 'bool':
                decls.append('    int %s = %s;' % (name, default or '0'))
                fmt = 'p'
                callargs.append('(bool)' + name)
            else:
                decls.append('    int %s = %s;' % (name, default or '0'))
                fmt = 'i'
                callargs.append('(%s)%s' % (C_ARG_TYPE[t], name))
            if default is None:
                fmt_req += fmt
            else:
                fmt_opt += fmt
            parse_args.append('&' + name)
        body.extend(decls)
        if e['args']:
            fmt = fmt_req + ('|' + fmt_opt if fmt_opt else '')
            body.append('    if (!PyArg_ParseTuple(args, "%s", %s)) return NULL;'
                        % (fmt, ', '.join(parse_args)))
        else:
            body.append('    (void)args;')
        call = c_call(e, callargs)
        if e['ret'] == 'void':
            body.append('    %s;' % call)
            body.append('    Py_RETURN_NONE;')
        elif e['ret'] == 'f32':
            body.append('    return Py_BuildValue("f", %s);' % call)
        elif e['ret'] == 'u32':
            body.append('    return Py_BuildValue("I", (unsigned int)%s);' % call)
        else:  # i32
            body.append('    return Py_BuildValue("i", (int)%s);' % call)
    elif e['kind'] == 'string_generator':
        body.append('    char s[%s];' % MAX_MSG)
        body.append('    void *state = NULL;')
        body.append('    int synth = 0;')
        body.append('    int include_fx = 1;')
        body.append('    if (!PyArg_ParseTuple(args, "i|p", &synth, &include_fx)) return NULL;')
        body.append('    PyObject *list_obj = PyList_New(0);')
        body.append('    if (list_obj == NULL) return NULL;')
        body.append('    do {')
        body.append('        state = %s((uint8_t)synth, s, %s, (bool)include_fx, state);'
                    % (e['c'], MAX_MSG))
        body.append('        if (strlen(s))')
        body.append('            PyList_Append(list_obj, PyUnicode_FromString(s));')
        body.append('    } while (state != NULL);')
        body.append('    return list_obj;')
    elif e['kind'] == 'string_out_malloc':
        body.append('    (void)args;')
        body.append('    int len = 0;')
        body.append('    char *dump = %s(&len);' % e['c'])
        body.append('    if (dump == NULL) return PyErr_NoMemory();')
        body.append('    PyObject *result = PyUnicode_FromStringAndSize(dump, len);')
        body.append('    free(dump);')
        body.append('    return result;')
    elif e['kind'] == 'bytes_out':
        body.append('    (void)args;')
        body.append('    uint8_t buf[%d];' % BYTES_OUT_BUF)
        body.append('    int n = %s((int16_t *)buf);' % e['c'])
        body.append('    if (n == 0) Py_RETURN_NONE;')
        body.append('    return PyBytes_FromStringAndSize((const char *)buf, n);')
    body.append('}')
    return fn, '\n'.join(body)


def gen_cpython():
    wrappers, rows = [], []
    for e in API:
        if 'py' not in e['platforms']:
            continue
        fn, code = gen_py_wrapper(e)
        wrappers.append(code)
        rows.append('{"%s", %s, METH_VARARGS, "%s"},' % (e['py'], fn, e['doc']))
    header = '// %s\n' % GENERATED_NOTE
    (ROOT / 'src' / 'amy_c_api_py.inc').write_text(header + '\n\n'.join(wrappers) + '\n')
    (ROOT / 'src' / 'amy_c_api_py_table.inc').write_text(
        header + '\n'.join(rows) + '\n')


# ------------------------------------------------------------ MicroPython ---

def gen_mp_wrapper(e):
    fn = 'amy_capi_mp_%s' % e['py']
    nreq = sum(1 for a in e['args'] if a[2] is None)
    nmax = len(e['args'])
    body = ['static mp_obj_t %s(size_t n_args, const mp_obj_t *args) {' % fn]
    if nmax == 0:
        body.append('    (void)n_args; (void)args;')
    elif nreq == nmax:
        body.append('    (void)n_args;')
    if e['kind'] == 'scalar':
        callargs = []
        for i, (name, t, default) in enumerate(e['args']):
            if default is None:
                src = 'args[%d]' % i
                if t == 'str':
                    body.append('    char *%s = (char *)mp_obj_str_get_str(%s);' % (name, src))
                    callargs.append(name)
                elif t == 'f32':
                    body.append('    float %s = (float)mp_obj_get_float(%s);' % (name, src))
                    callargs.append(name)
                elif t == 'bool':
                    body.append('    bool %s = mp_obj_is_true(%s);' % (name, src))
                    callargs.append(name)
                else:
                    body.append('    %s %s = (%s)mp_obj_get_int(%s);'
                                % (C_ARG_TYPE[t], name, C_ARG_TYPE[t], src))
                    callargs.append(name)
            else:
                cond = '(n_args > %d)' % i
                src = 'args[%d]' % i
                if t == 'f32':
                    body.append('    float %s = %s ? (float)mp_obj_get_float(%s) : %s;'
                                % (name, cond, src, default))
                elif t == 'bool':
                    body.append('    bool %s = %s ? mp_obj_is_true(%s) : (bool)%s;'
                                % (name, cond, src, default))
                else:
                    body.append('    %s %s = %s ? (%s)mp_obj_get_int(%s) : (%s)%s;'
                                % (C_ARG_TYPE[t], name, cond, C_ARG_TYPE[t], src,
                                   C_ARG_TYPE[t], default))
                callargs.append(name)
        call = c_call(e, callargs)
        if e['ret'] == 'void':
            body.append('    %s;' % call)
            body.append('    return mp_const_none;')
        elif e['ret'] == 'f32':
            body.append('    return mp_obj_new_float(%s);' % call)
        elif e['ret'] == 'u32':
            body.append('    return mp_obj_new_int_from_uint(%s);' % call)
        else:
            body.append('    return mp_obj_new_int(%s);' % call)
    elif e['kind'] == 'string_generator':
        body.append('    char s[%s];' % MAX_MSG)
        body.append('    void *state = NULL;')
        body.append('    uint8_t synth = (uint8_t)mp_obj_get_int(args[0]);')
        body.append('    bool include_fx = (n_args > 1) ? mp_obj_is_true(args[1]) : true;')
        body.append('    mp_obj_t list = mp_obj_new_list(0, NULL);')
        body.append('    do {')
        body.append('        state = %s(synth, s, %s, include_fx, state);' % (e['c'], MAX_MSG))
        body.append('        int slen = strlen(s);')
        body.append('        if (slen)')
        body.append('            mp_obj_list_append(list, mp_obj_new_str(s, slen));')
        body.append('    } while (state != NULL);')
        body.append('    return list;')
    elif e['kind'] == 'string_out_malloc':
        body.append('    int len = 0;')
        body.append('    char *dump = %s(&len);' % e['c'])
        body.append('    if (dump == NULL) mp_raise_msg(&mp_type_MemoryError, NULL);')
        body.append('    mp_obj_t result = mp_obj_new_str(dump, len);')
        body.append('    free(dump);')
        body.append('    return result;')
    elif e['kind'] == 'bytes_out':
        body.append('    uint8_t buf[%d];' % BYTES_OUT_BUF)
        body.append('    int n = %s((int16_t *)buf);' % e['c'])
        body.append('    if (n == 0) return mp_const_none;')
        body.append('    return mp_obj_new_bytes(buf, n);')
    body.append('}')
    nreq_all = 0 if e['kind'] in ('string_out_malloc', 'bytes_out') else nreq
    nmax_all = 0 if e['kind'] in ('string_out_malloc', 'bytes_out') else nmax
    body.append('static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(%s_obj, %d, %d, %s);'
                % (fn, nreq_all, nmax_all, fn))
    return fn, '\n'.join(body)


def gen_micropython():
    wrappers, rows = [], []
    for e in API:
        if 'mp' not in e['platforms']:
            continue
        fn, code = gen_mp_wrapper(e)
        wrappers.append(code)
        rows.append('{ MP_ROM_QSTR(MP_QSTR_%s), MP_ROM_PTR(&%s_obj) },' % (mp_name(e), fn))
    header = '// %s\n' % GENERATED_NOTE
    (ROOT / 'src' / 'amy_c_api_mp.inc').write_text(header + '\n\n'.join(wrappers) + '\n')
    (ROOT / 'src' / 'amy_c_api_mp_table.inc').write_text(header + '\n'.join(rows) + '\n')


# ------------------------------------------------------------------- Web ----

def js_cwrap(e):
    if e['kind'] != 'scalar':
        return None
    ret = 'null' if e['ret'] == 'void' else "'number'"
    argts = []
    for name, t, default in e['args']:
        argts.append("'string'" if t == 'str' else "'number'")
    return "am.cwrap('%s', %s, [%s])" % (e['c'], ret, ', '.join(argts))


def gen_web():
    lines = []
    lines.append('// %s' % GENERATED_NOTE)
    lines.append('// Binds the AMY C API against a loaded emscripten module instance.')
    lines.append('// Returns an object with one function per API entry; register it as')
    lines.append("// a MicroPython JS module and run AMY_C_API_PY_INSTALL to install the")
    lines.append('// canonical amy.<name> entry points (plus legacy tulip.amy_* redirects).')
    lines.append('function amy_c_api_bind(am) {')
    lines.append('  function _read_cstr(ptr, maxLen) {')
    lines.append('    var heap = am.HEAPU8;')
    lines.append('    var out = "";')
    lines.append('    var end = Math.min(heap.length, ptr + maxLen);')
    lines.append('    for (var i = ptr; i < end; i++) {')
    lines.append('      var b = heap[i];')
    lines.append('      if (b === 0) break;')
    lines.append('      out += String.fromCharCode(b);')
    lines.append('    }')
    lines.append('    return out;')
    lines.append('  }')
    lines.append('  var api = {};')
    for e in API:
        if 'web' not in e['platforms']:
            continue
        name = e['py']
        if e['kind'] == 'scalar':
            has_bool = any(t == 'bool' for _, t, _ in e['args'])
            has_default = any(d is not None for _, _, d in e['args'])
            raw = js_cwrap(e)
            if not (has_bool or has_default):
                lines.append('  api.%s = %s;' % (name, raw))
            else:
                argnames = [a[0] for a in e['args']]
                lines.append('  api._raw_%s = %s;' % (name, raw))
                lines.append('  api.%s = function(%s) {' % (name, ', '.join(argnames)))
                conv = []
                for aname, t, default in e['args']:
                    if default is not None:
                        lines.append('    if (%s === undefined) %s = %s;' % (aname, aname, default))
                    if t == 'bool':
                        conv.append('%s ? 1 : 0' % aname)
                    else:
                        conv.append(aname)
                lines.append('    return api._raw_%s(%s);' % (name, ', '.join(conv)))
                lines.append('  };')
        elif e['kind'] == 'string_generator':
            lines.append('  // Drives the C generator; returns wire commands joined by newline')
            lines.append('  // ("" if the synth has no state). Keeping the cross-boundary type a')
            lines.append('  // plain string avoids JS-array proxying in MicroPython.')
            lines.append('  api.%s = function(synth, include_fx) {' % name)
            lines.append('    if (include_fx === undefined) include_fx = 1;')
            lines.append('    var bufferPtr = am._malloc(1024);')
            lines.append('    if (!bufferPtr) return "";')
            lines.append('    var lines = [];')
            lines.append('    var state = 0;')
            lines.append('    var iterations = 0;')
            lines.append('    try {')
            lines.append('      do {')
            lines.append('        state = am._%s(synth, bufferPtr, 1024, include_fx ? 1 : 0, state);'
                         % e['c'])
            lines.append('        var wire = _read_cstr(bufferPtr, 1024).trim();')
            lines.append('        if (wire) lines.push(wire);')
            lines.append('        if (++iterations >= 500) break;')
            lines.append('      } while (state != 0);')
            lines.append('    } finally {')
            lines.append('      am._free(bufferPtr);')
            lines.append('    }')
            lines.append("    return lines.join('\\n');")
            lines.append('  };')
        elif e['kind'] == 'string_out_malloc':
            lines.append('  api.%s = function() {' % name)
            lines.append('    var lenPtr = am._malloc(4);')
            lines.append('    if (!lenPtr) return "";')
            lines.append('    var strPtr = am._%s(lenPtr);' % e['c'])
            lines.append('    var out = "";')
            lines.append('    if (strPtr) {')
            lines.append('      var len = new DataView(am.HEAPU8.buffer).getInt32(lenPtr, true);')
            lines.append('      out = _read_cstr(strPtr, len + 1);')
            lines.append('      am._free(strPtr);')
            lines.append('    }')
            lines.append('    am._free(lenPtr);')
            lines.append('    return out;')
            lines.append('  };')
        elif e['kind'] == 'bytes_out':
            lines.append('  api.%s = function() {' % name)
            lines.append('    var ptr = api._buf_%s || (api._buf_%s = am._malloc(%d));'
                         % (name, name, BYTES_OUT_BUF))
            lines.append('    var n = am._%s(ptr);' % e['c'])
            lines.append('    if (!n) return null;')
            lines.append('    return am.HEAPU8.slice(ptr, ptr + n);')
            lines.append('  };')
    lines.append('  return api;')
    lines.append('}')
    lines.append('')

    # The MicroPython install snippet: canonical amy.<name> backends plus the
    # legacy tulip.amy_* redirects some tulipcc code still calls.
    py = []
    py.append('import amy, tulip')
    py.append('import amy_c_api_js as _acj')
    for e in API:
        if 'web' not in e['platforms']:
            continue
        if e['kind'] == 'string_generator':
            py.append('amy._%s = lambda synth, include_fx=True: '
                      "[c for c in _acj.%s(synth, include_fx).split('\\n') if c]"
                      % (e['py'], e['py']))
            py.append('tulip.%s = amy._%s' % (mp_name(e), e['py']))
        else:
            py.append('amy._%s = _acj.%s' % (e['py'], e['py']))
            py.append('tulip.%s = _acj.%s' % (mp_name(e), e['py']))
    # Escape backslashes before quotes: a python-source '\n' inside a line must
    # survive the JS string literal as backslash-n, not become a real newline.
    install = '\\n'.join(line.replace('\\', '\\\\').replace("'", "\\'") for line in py)
    lines.append('// Run this in MicroPython after registerJsModule("amy_c_api_js", api).')
    lines.append("var AMY_C_API_PY_INSTALL = '%s';" % install)
    lines.append('')
    (ROOT / 'src' / 'amy_c_api.generated.js').write_text('\n'.join(lines))

    exports = []
    for e in API:
        if 'web' in e['platforms']:
            exports.append("'_%s'" % e['c'])
    frag = ('# %s\n' % GENERATED_NOTE
            + 'AMY_C_API_EXPORTED_FUNCTIONS = %s\n' % ', '.join(exports))
    (ROOT / 'src' / 'amy_c_api_exports.mk').write_text(frag)


# ----------------------------------------------------------------- Godot ----

GD_RET = {'void': 'void', 'u32': 'int64_t', 'i32': 'int64_t', 'f32': 'double'}
GD_ARG = {'u8': 'int64_t', 'u16': 'int64_t', 'i32': 'int64_t', 'u32': 'int64_t',
          'f32': 'double', 'bool': 'bool', 'str': 'const String &'}


def gen_godot():
    # Three fragments: method declarations for the class body in
    # amy_gdextension.h, out-of-line definitions for amy_gdextension.cpp
    # (which includes amy.h inside extern "C"), and ClassDB::bind_method rows
    # for _bind_methods(). C calls are ::-qualified so a member sharing the C
    # function's name (e.g. sequencer_ticks) can't shadow it into recursion.
    decls, impls, binds = [], [], []
    for e in API:
        if 'gd' not in e['platforms']:
            continue
        name = e['py']
        if e['kind'] == 'scalar':
            declparams, defparams, callargs = [], [], []
            for aname, t, default in e['args']:
                p = '%s %s' % (GD_ARG[t], aname)
                defparams.append(p)
                if default is not None:
                    p += ' = %s' % default
                declparams.append(p)
                if t == 'str':
                    callargs.append('(char *)%s.utf8().get_data()' % aname)
                else:
                    callargs.append('(%s)%s' % (C_ARG_TYPE[t], aname))
            call = '::%s(%s)' % (e['c'], ', '.join(callargs))
            decls.append('\t%s %s(%s);' % (GD_RET[e['ret']], name, ', '.join(declparams)))
            if e['ret'] == 'void':
                bodyline = '%s;' % call
            else:
                bodyline = 'return (%s)%s;' % (GD_RET[e['ret']], call)
            impls.append('%s AmySynth::%s(%s) { %s }'
                         % (GD_RET[e['ret']], name, ', '.join(defparams), bodyline))
        elif e['kind'] == 'string_out_malloc':
            decls.append('\tString %s();' % name)
            impls.append('String AmySynth::%s() {' % name)
            impls.append('\tint len = 0;')
            impls.append('\tchar *dump = ::%s(&len);' % e['c'])
            impls.append('\tif (dump == NULL) return String();')
            impls.append('\tString result = String::utf8(dump, len);')
            # ::-qualified: GDCLASS defines a static member free() that shadows libc's
            impls.append('\t::free(dump);')
            impls.append('\treturn result;')
            impls.append('}')
        else:
            continue  # other kinds not exposed to Godot yet
        argnames = ['"%s"' % a[0] for a in e['args']]
        dm = ['"%s"' % name] + argnames
        binds.append('\tClassDB::bind_method(D_METHOD(%s), &AmySynth::%s%s);'
                     % (', '.join(dm), name,
                        ''.join(', DEFVAL(%s)' % a[2] for a in e['args'] if a[2] is not None)))
    header = '// %s\n' % GENERATED_NOTE
    (ROOT / 'godot' / 'src' / 'amy_c_api_gd_methods.inc').write_text(
        header + '\n'.join(decls) + '\n')
    (ROOT / 'godot' / 'src' / 'amy_c_api_gd_impl.inc').write_text(
        header + '\n'.join(impls) + '\n')
    (ROOT / 'godot' / 'src' / 'amy_c_api_gd_bind.inc').write_text(
        header + '\n'.join(binds) + '\n')


DOCS_BEGIN = '<!-- BEGIN GENERATED C API DOCS - scripts/gen_amy_c_api.py -->'
DOCS_END = '<!-- END GENERATED C API DOCS -->'

DOC_C_TYPE = {'u8': 'uint8_t', 'u16': 'uint16_t', 'i32': 'int', 'u32': 'uint32_t',
              'f32': 'float', 'bool': 'bool', 'str': 'char *'}
DOC_C_RET = {'void': 'void', 'u32': 'uint32_t', 'i32': 'int', 'f32': 'float'}


def doc_c_signature(e):
    if e['kind'] == 'string_generator':
        return ('void *%s(uint8_t synth, char *s, size_t len, bool include_fx, '
                'void *state)' % e['c'])
    if e['kind'] == 'string_out_malloc':
        return 'char *%s(int *out_len)' % e['c']
    if e['kind'] == 'bytes_out':
        return 'int %s(int16_t *samples)' % e['c']
    args = ', '.join('%s %s' % (DOC_C_TYPE[t], n) for n, t, _ in e['args'])
    return '%s %s(%s)' % (DOC_C_RET[e['ret']], e['c'], args)


def doc_py_signature(e):
    parts = []
    for n, t, d in e['args']:
        if d is None:
            parts.append(n)
        else:
            parts.append('%s=%s' % (n, {'1': 'True', '0': 'False'}[d] if t == 'bool' else d))
    return 'amy.%s(%s)' % (e['py'], ', '.join(parts))


def render_docs_block():
    out = []
    out.append('| Python (all platforms) | C function | MicroPython alias | Godot (`AmySynth`) | What it does |')
    out.append('|---|---|---|---|---|')
    for e in API:
        py = '`%s`' % doc_py_signature(e) if e['py_public'] else '`amy.%s` (low-level; see below)' % e['py']
        gd = '`%s`' % e['py'] if 'gd' in e['platforms'] else '—'
        mp = '`tulip.%s`' % mp_name(e) if 'mp' in e['platforms'] else '—'
        if 'py' not in e['platforms'] or 'web' not in e['platforms']:
            plats = [p for p, label in (('py', 'CPython'), ('mp', 'MicroPython'),
                                        ('web', 'web')) if p in e['platforms']]
            py += ' *(%s only)*' % '/'.join({'py': 'CPython', 'mp': 'MicroPython',
                                             'web': 'web'}[p] for p in plats)
        out.append('| %s | `%s` | %s | %s | %s |'
                   % (py, doc_c_signature(e), mp, gd, e['doc']))
    out.append('')
    out.append('Notes:')
    out.append('')
    out.append('- `amy.get_synth_commands(synth, ...)` is a hand-written wrapper in')
    out.append('  `amy/__init__.py` adding `patch_num`/`dest_synth` handling on top of the')
    out.append('  generated backend (which drives the C generator `yield_synth_commands`);')
    out.append('  it returns the commands newline-joined into one string.')
    out.append('- `amy.get_output_buffer()` / `amy.get_input_buffer()` return up to 1024')
    out.append('  bytes of interleaved int16 samples (`None` when no block is ready).')
    out.append('- `amy.dump_state()` returns the complete replayable engine state as a')
    out.append('  newline-separated wire-command string.')
    return '\n'.join(out) + '\n'


def gen_docs(check=False):
    path = ROOT / 'docs' / 'api.md'
    text = path.read_text()
    lines = text.split('\n')
    try:
        b = next(i for i, l in enumerate(lines) if l.strip() == DOCS_BEGIN)
        e = next(i for i, l in enumerate(lines) if l.strip() == DOCS_END)
    except StopIteration:
        raise SystemExit('Could not find %r / %r markers in %s' % (DOCS_BEGIN, DOCS_END, path))
    if e <= b:
        raise SystemExit('Markers out of order in %s' % path)
    new = lines[:b + 1] + render_docs_block().split('\n') + lines[e:]
    new_text = '\n'.join(new)
    if new_text != text:
        if check:
            return False
        path.write_text(new_text)
    return True


GD_SCRIPT_BEGIN = '# BEGIN GENERATED C API - scripts/gen_amy_c_api.py'
GD_SCRIPT_END = '# END GENERATED C API'

GD_SCRIPT_RET = {'void': 'void', 'u32': 'int', 'i32': 'int', 'f32': 'float'}
GD_SCRIPT_RET_DEFAULT = {'int': '0', 'float': '0.0', 'String': '""'}
GD_SCRIPT_ARG = {'u8': 'int', 'u16': 'int', 'i32': 'int', 'u32': 'int',
                 'f32': 'float', 'bool': 'bool'}


def render_gd_script_block():
    out = []
    for e in API:
        if 'gd' not in e['platforms']:
            continue
        name = e['py']
        if e['kind'] == 'scalar':
            ret = GD_SCRIPT_RET[e['ret']]
        elif e['kind'] == 'string_out_malloc':
            ret = 'String'
        else:
            continue
        params = []
        for aname, t, default in e['args']:
            p = '%s: %s' % (aname, GD_SCRIPT_ARG[t])
            if default is not None:
                p += ' = %s' % default
            params.append(p)
        argnames = [a[0] for a in e['args']]
        js_args = ', '.join('%s' for _ in argnames)
        js_fmt = 'amy_c_api.%s(%s)' % (name, js_args)
        out.append('## %s' % e['doc'])
        out.append('func %s(%s) -> %s:' % (name, ', '.join(params), ret))
        if ret == 'void':
            out.append('\tif _is_web:')
            if argnames:
                out.append('\t\tJavaScriptBridge.eval("amy_c_api && %s" %% [%s])'
                           % (js_fmt, ', '.join('str(%s)' % a for a in argnames)))
            else:
                out.append('\t\tJavaScriptBridge.eval("amy_c_api && %s")'
                           % (js_fmt % ()))
            out.append('\telif _synth:')
            out.append('\t\t_synth.call(%s)'
                       % ', '.join(['"%s"' % name] + argnames))
        else:
            fallback = GD_SCRIPT_RET_DEFAULT[ret]
            js_null = 'null'
            out.append('\tif _is_web:')
            if argnames:
                out.append('\t\tvar v: Variant = JavaScriptBridge.eval("amy_c_api ? %s : %s" %% [%s], true)'
                           % (js_fmt, js_null, ', '.join('str(%s)' % a for a in argnames)))
            else:
                out.append('\t\tvar v: Variant = JavaScriptBridge.eval("amy_c_api ? %s : %s", true)'
                           % (js_fmt % (), js_null))
            out.append('\t\treturn %s if v == null else %s(v)' % (fallback, ret))
            out.append('\tif _synth:')
            out.append('\t\treturn _synth.call(%s)'
                       % ', '.join(['"%s"' % name] + argnames))
            out.append('\treturn %s' % fallback)
        out.append('')
    return '\n'.join(out).rstrip() + '\n'


def gen_gd_script(check=False):
    path = ROOT / 'godot' / 'amy.gd'
    text = path.read_text()
    lines = text.split('\n')
    try:
        b = next(i for i, l in enumerate(lines) if l.strip() == GD_SCRIPT_BEGIN)
        e = next(i for i, l in enumerate(lines) if l.strip() == GD_SCRIPT_END)
    except StopIteration:
        raise SystemExit('Could not find %r / %r markers in %s'
                         % (GD_SCRIPT_BEGIN, GD_SCRIPT_END, path))
    if e <= b:
        raise SystemExit('Markers out of order in %s' % path)
    new = lines[:b + 1] + render_gd_script_block().split('\n') + lines[e:]
    new_text = '\n'.join(new)
    if new_text != text:
        if check:
            return False
        path.write_text(new_text)
    return True


# ------------------------------------------------- amy/__init__.py block ----

PY_BEGIN = '# BEGIN GENERATED - scripts/gen_amy_c_api.py'
PY_END = '# END GENERATED - scripts/gen_amy_c_api.py'


def render_py_block():
    out = []
    out.append('# One backend resolver per C API function: prefer the CPython c_amy')
    out.append('# module, then the linked-MicroPython tulip module. On web builds both')
    out.append('# resolve to _capi_missing at import; the JS side installs amy._<name>')
    out.append('# backends after the WASM module loads (see amy_c_api.generated.js).')
    out.append('def _capi_missing(name):')
    out.append('    def _f(*args, **kwargs):')
    out.append('        raise NotImplementedError("amy.%s is not available on this platform" % name)')
    out.append('    return _f')
    out.append('')
    out.append('try:')
    out.append('    import c_amy as _capi_c_amy')
    out.append('except ImportError:')
    out.append('    _capi_c_amy = None')
    out.append('try:')
    out.append('    import tulip as _capi_tulip')
    out.append('except ImportError:')
    out.append('    _capi_tulip = None')
    out.append('')
    out.append('def _capi_resolve(py_name, mp_name):')
    out.append('    f = getattr(_capi_c_amy, py_name, None) if _capi_c_amy else None')
    out.append('    if f is None and _capi_tulip:')
    out.append('        f = getattr(_capi_tulip, mp_name, None)')
    out.append('    return f if f is not None else _capi_missing(py_name)')
    out.append('')
    for e in API:
        out.append("_%s = _capi_resolve('%s', '%s')" % (e['py'], e['py'], mp_name(e)))
    out.append('')
    for e in API:
        if not e['py_public']:
            continue
        argsig, callsig = [], []
        for aname, t, default in e['args']:
            if default is None:
                argsig.append(aname)
            else:
                pydef = {'1': 'True', '0': 'False'}[default] if t == 'bool' else default
                argsig.append('%s=%s' % (aname, pydef))
            callsig.append(aname)
        out.append('def %s(%s):' % (e['py'], ', '.join(argsig)))
        out.append('    """%s"""' % e['doc'])
        out.append('    return _%s(%s)' % (e['py'], ', '.join(callsig)))
        out.append('')
    return '\n'.join(out).rstrip() + '\n'


def gen_py_init(check=False):
    path = ROOT / 'amy' / '__init__.py'
    text = path.read_text()
    lines = text.split('\n')
    try:
        b = next(i for i, l in enumerate(lines) if l.strip() == PY_BEGIN)
        e = next(i for i, l in enumerate(lines) if l.strip() == PY_END)
    except StopIteration:
        raise SystemExit('Could not find %r / %r markers in %s' % (PY_BEGIN, PY_END, path))
    if e <= b:
        raise SystemExit('Markers out of order in %s' % path)
    new = lines[:b + 1] + render_py_block().split('\n') + lines[e:]
    new_text = '\n'.join(new)
    if new_text != text:
        if check:
            return False
        path.write_text(new_text)
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true',
                    help='verify all generated outputs are up to date')
    opts = ap.parse_args()

    outputs = [
        ROOT / 'src' / 'amy_c_api_py.inc',
        ROOT / 'src' / 'amy_c_api_py_table.inc',
        ROOT / 'src' / 'amy_c_api_mp.inc',
        ROOT / 'src' / 'amy_c_api_mp_table.inc',
        ROOT / 'src' / 'amy_c_api.generated.js',
        ROOT / 'src' / 'amy_c_api_exports.mk',
        ROOT / 'godot' / 'src' / 'amy_c_api_gd_methods.inc',
        ROOT / 'godot' / 'src' / 'amy_c_api_gd_impl.inc',
        ROOT / 'godot' / 'src' / 'amy_c_api_gd_bind.inc',
    ]
    before = {p: (p.read_text() if p.exists() else None) for p in outputs}

    gen_cpython()
    gen_micropython()
    gen_web()
    gen_godot()
    init_fresh = gen_py_init(check=opts.check)
    gd_fresh = gen_gd_script(check=opts.check)
    docs_fresh = gen_docs(check=opts.check)

    if opts.check:
        stale = [str(p.relative_to(ROOT)) for p in outputs if before[p] != p.read_text()]
        if not init_fresh:
            stale.append('amy/__init__.py')
        if not gd_fresh:
            stale.append('godot/amy.gd')
        if not docs_fresh:
            stale.append('docs/api.md')
        # restore originals so --check has no side effects
        for p in outputs:
            if before[p] is None:
                p.unlink()
            else:
                p.write_text(before[p])
        if stale:
            print('stale generated C API files (run: python3 scripts/gen_amy_c_api.py):')
            for s in stale:
                print('  ' + s)
            sys.exit(1)
        print('amy C API generated files are up to date')
    else:
        for p in outputs:
            print('wrote %s' % p.relative_to(ROOT))
        print('updated amy/__init__.py generated block')
        print('updated godot/amy.gd generated C API block')
        print('updated docs/api.md generated C API table')


if __name__ == '__main__':
    main()
