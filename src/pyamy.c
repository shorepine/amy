#if !defined(ESP_PLATFORM) && !defined(PICO_ON_DEVICE) && !defined(ARDUINO)

#include <Python.h>
#include <math.h>
#include <limits.h>
#include <string.h>
#include "amy.h"
#include "amy_midi.h"
#include "libminiaudio-audio.h"

// Python module wrapper for AMY commands

static PyObject * send_wrapper(PyObject *self, PyObject *args) {
    char *arg1;
    if (! PyArg_ParseTuple(args, "s", &arg1)) {
        return NULL;
    }
    amy_add_message(arg1);
    return Py_None;
}


static int parse_live_kwarg(amy_config_t *cfg, const char *key, PyObject *value) {
    long lv = 0;
    long long llv = 0;

    if (strcmp(key, "chorus") == 0) {
        lv = PyLong_AsLong(value);
        if (PyErr_Occurred()) return -1;
        cfg->features.chorus = (lv != 0);
        return 0;
    } else if (strcmp(key, "reverb") == 0) {
        lv = PyLong_AsLong(value);
        if (PyErr_Occurred()) return -1;
        cfg->features.reverb = (lv != 0);
        return 0;
    } else if (strcmp(key, "echo") == 0) {
        lv = PyLong_AsLong(value);
        if (PyErr_Occurred()) return -1;
        cfg->features.echo = (lv != 0);
        return 0;
    } else if (strcmp(key, "audio_in") == 0) {
        lv = PyLong_AsLong(value);
        if (PyErr_Occurred()) return -1;
        cfg->features.audio_in = (lv != 0);
        return 0;
    } else if (strcmp(key, "default_synths") == 0) {
        lv = PyLong_AsLong(value);
        if (PyErr_Occurred()) return -1;
        cfg->features.default_synths = (lv != 0);
        return 0;
    } else if (strcmp(key, "partials") == 0) {
        lv = PyLong_AsLong(value);
        if (PyErr_Occurred()) return -1;
        cfg->features.partials = (lv != 0);
        return 0;
    } else if (strcmp(key, "custom") == 0) {
        lv = PyLong_AsLong(value);
        if (PyErr_Occurred()) return -1;
        cfg->features.custom = (lv != 0);
        return 0;
    } else if (strcmp(key, "startup_bleep") == 0) {
        lv = PyLong_AsLong(value);
        if (PyErr_Occurred()) return -1;
        cfg->features.startup_bleep = (lv != 0);
        return 0;
    } else if (strcmp(key, "max_oscs") == 0) {
        lv = PyLong_AsLong(value);
        if (PyErr_Occurred()) return -1;
        if (lv < 0 || lv > UINT16_MAX) {
            PyErr_SetString(PyExc_ValueError, "max_oscs must be in range [0, 65535]");
            return -1;
        }
        cfg->max_oscs = (uint16_t)lv;
        return 0;
    } else if (strcmp(key, "ks_oscs") == 0) {
        lv = PyLong_AsLong(value);
        if (PyErr_Occurred()) return -1;
        if (lv < 0 || lv > UINT8_MAX) {
            PyErr_SetString(PyExc_ValueError, "ks_oscs must be in range [0, 255]");
            return -1;
        }
        cfg->ks_oscs = (uint8_t)lv;
        return 0;
    } else if (strcmp(key, "max_sequencer_tags") == 0) {
        llv = PyLong_AsLongLong(value);
        if (PyErr_Occurred()) return -1;
        if (llv < 0 || (unsigned long long)llv > UINT32_MAX) {
            PyErr_SetString(PyExc_ValueError, "max_sequencer_tags must be in range [0, 4294967295]");
            return -1;
        }
        cfg->max_sequencer_tags = (uint32_t)llv;
        return 0;
    } else if (strcmp(key, "max_voices") == 0) {
        llv = PyLong_AsLongLong(value);
        if (PyErr_Occurred()) return -1;
        if (llv < 0 || (unsigned long long)llv > UINT32_MAX) {
            PyErr_SetString(PyExc_ValueError, "max_voices must be in range [0, 4294967295]");
            return -1;
        }
        cfg->max_voices = (uint32_t)llv;
        return 0;
    } else if (strcmp(key, "max_synths") == 0) {
        llv = PyLong_AsLongLong(value);
        if (PyErr_Occurred()) return -1;
        if (llv < 0 || (unsigned long long)llv > UINT32_MAX) {
            PyErr_SetString(PyExc_ValueError, "max_synths must be in range [0, 4294967295]");
            return -1;
        }
        cfg->max_synths = (uint32_t)llv;
        return 0;
    } else if (strcmp(key, "max_memory_patches") == 0) {
        llv = PyLong_AsLongLong(value);
        if (PyErr_Occurred()) return -1;
        if (llv < 0 || (unsigned long long)llv > UINT32_MAX) {
            PyErr_SetString(PyExc_ValueError, "max_memory_patches must be in range [0, 4294967295]");
            return -1;
        }
        cfg->max_memory_patches = (uint32_t)llv;
        return 0;
    } else if (strcmp(key, "capture_device_id") == 0) {
        lv = PyLong_AsLong(value);
        if (PyErr_Occurred()) return -1;
        if (lv < INT8_MIN || lv > INT8_MAX) {
            PyErr_SetString(PyExc_ValueError, "capture_device_id must be in range [-128, 127]");
            return -1;
        }
        cfg->capture_device_id = (int8_t)lv;
        return 0;
    } else if (strcmp(key, "playback_device_id") == 0) {
        lv = PyLong_AsLong(value);
        if (PyErr_Occurred()) return -1;
        if (lv < INT8_MIN || lv > INT8_MAX) {
            PyErr_SetString(PyExc_ValueError, "playback_device_id must be in range [-128, 127]");
            return -1;
        }
        cfg->playback_device_id = (int8_t)lv;
        return 0;
    }

    PyErr_Format(PyExc_TypeError, "live() got an unexpected keyword argument '%s'", key);
    return -1;
}

static PyObject * live_wrapper(PyObject *self, PyObject *args, PyObject *kwargs) {
    amy_stop();
    amy_config_t amy_config = amy_global.config;
    Py_ssize_t pos = 0;
    PyObject *key_obj = NULL;
    PyObject *value_obj = NULL;

    if (PyTuple_Size(args) != 0) {
        PyErr_SetString(PyExc_TypeError, "live() no longer accepts positional args; use keyword args like live(playback_device_id=..., capture_device_id=...)");
        return NULL;
    }

    amy_config.features.audio_in = 1;

    while (kwargs != NULL && PyDict_Next(kwargs, &pos, &key_obj, &value_obj)) {
        const char *key = PyUnicode_AsUTF8(key_obj);
        if (key == NULL) return NULL;
        if (parse_live_kwarg(&amy_config, key, value_obj) != 0) {
            return NULL;
        }
    }

    amy_config.audio = AMY_AUDIO_IS_MINIAUDIO;
    amy_start(amy_config); // initializes amy 
    Py_RETURN_NONE;
}

static PyObject * amystop_wrapper(PyObject *self, PyObject *args) {
    amy_stop();
    return Py_None;
}

static PyObject * amystart_wrapper(PyObject *self, PyObject *args) {
    int default_synths = 0;
    if(PyTuple_Size(args) == 1) {
        PyArg_ParseTuple(args, "i", &default_synths);
    }
    amy_config_t amy_config = amy_global.config; // amy_default_config();
    amy_config.features.default_synths = default_synths;
    amy_start(amy_config); // initializes amy 
    return Py_None;
}

static PyObject * config_wrapper(PyObject *self, PyObject *args) {
    PyObject* ret = PyList_New(5); 
    PyList_SetItem(ret, 0, Py_BuildValue("i", AMY_BLOCK_SIZE));
    PyList_SetItem(ret, 1, Py_BuildValue("i", AMY_CORES));
    PyList_SetItem(ret, 2, Py_BuildValue("i", AMY_NCHANS));
    PyList_SetItem(ret, 3, Py_BuildValue("i", AMY_SAMPLE_RATE));
    PyList_SetItem(ret, 4, Py_BuildValue("i", AMY_OSCS));
    return ret;
}

static PyObject * render_wrapper(PyObject *self, PyObject *args) {
    int16_t * result = amy_simple_fill_buffer();
    // Create a python list of ints (they are signed shorts that come back)
    uint16_t bs = AMY_BLOCK_SIZE;
    if(AMY_NCHANS == 2) {
        bs = AMY_BLOCK_SIZE*2;
    }
    PyObject* ret = PyList_New(bs); 
    for (int i = 0; i < bs; i++) {
        PyObject* python_int = Py_BuildValue("i", result[i]);
        PyList_SetItem(ret, i, python_int);
    }
    return ret;
}

static PyObject * inject_midi_wrapper(PyObject *self, PyObject *args) {
#define MAX_MIDI_ARGS 16
    int data[MAX_MIDI_ARGS];
    uint8_t byte_data[MAX_MIDI_ARGS];
    uint32_t time = AMY_UNSET_VALUE(time);
    // But for now we accept only exactly 3 or 4 values: [time,] midi_bytes0..2
    if (! PyArg_ParseTuple(args, "iiii", &time, &data[0], &data[1], &data[2]))
      if (! PyArg_ParseTuple(args, "iii", &data[0], &data[1], &data[2]))
        return NULL;
    uint8_t sysex = 0;
    for (int i = 0; i < 3; ++i)  byte_data[i] = (uint8_t)data[i];
    amy_event_midi_message_received(byte_data, 3, sysex, time);
    return Py_None;
}


static PyMethodDef c_amyMethods[] = {
    {"render_to_list", render_wrapper, METH_VARARGS, "Render audio"},
    {"send_wire", send_wrapper, METH_VARARGS, "Send a message"},
    {"live", (PyCFunction)live_wrapper, METH_VARARGS | METH_KEYWORDS, "Live AMY"},
    {"start", amystart_wrapper, METH_VARARGS, "Start AMY"},
    {"stop", amystop_wrapper, METH_VARARGS, "Stop AMY"},
    {"config", config_wrapper, METH_VARARGS, "Return config"},
    {"inject_midi", inject_midi_wrapper, METH_VARARGS, "Inject a MIDI message"},
    { NULL, NULL, 0, NULL }
};

static struct PyModuleDef c_amyDef =
{
    PyModuleDef_HEAD_INIT,
    "c_amy", /* name of module */
    "",          /* module documentation, may be NULL */
    -1,          /* size of per-interpreter state of the module, or -1 if the module keeps state in global variables. */
    c_amyMethods
};

PyMODINIT_FUNC PyInit_c_amy(void)
{
    // This is the first time it's called, so there's nothing in amy_global.
    // But for later calls, we copy the existing amy_global.config.
    amy_config_t amy_config = amy_default_config();
    amy_start(amy_config);
    return PyModule_Create(&c_amyDef);
}
#endif
