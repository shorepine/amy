#if !defined(ESP_PLATFORM) && !defined(PICO_ON_DEVICE) && !defined(ARDUINO)

#include <Python.h>
#include <math.h>
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


static PyObject * live_wrapper(PyObject *self, PyObject *args) {
    int arg1 = -1; int arg2 = -1;
    if(PyTuple_Size(args) == 2) {
        PyArg_ParseTuple(args, "ii", &arg1, &arg2);
        amy_global.config.playback_device_id = arg1;
        amy_global.config.capture_device_id = arg2;
    } else {
        amy_global.config.playback_device_id = -1;
        amy_global.config.capture_device_id = -1;
    }
    amy_live_start();
    return Py_None;
}

static PyObject * pause_wrapper(PyObject *self, PyObject *args) {
    amy_live_stop();
    return Py_None;
}

static PyObject * amystop_wrapper(PyObject *self, PyObject *args) {
    amy_stop();
    return Py_None;
}

static PyObject * amystart_wrapper(PyObject *self, PyObject *args) {
    amy_config_t amy_config = amy_default_config();
    amy_start(amy_config); // initializes amy 
    return Py_None;
}

static PyObject * amystart_no_default_wrapper(PyObject *self, PyObject *args) {
    amy_config_t amy_config = amy_default_config();
    amy_config.features.default_synths = 0;
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
    char *arg1;
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
    {"live", live_wrapper, METH_VARARGS, "Live AMY"},
    {"pause", pause_wrapper, METH_VARARGS, "Pause AMY"},
    {"start_no_default", amystart_no_default_wrapper, METH_VARARGS, "Start AMY"},
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
    amy_config_t amy_config = amy_default_config();
    amy_start(amy_config);
    return PyModule_Create(&c_amyDef);
}
#endif
