#if !defined(ESP_PLATFORM) && !defined(PICO_ON_DEVICE) && !defined(ARDUINO)

#include <Python.h>
#include <math.h>
#include "amy.h"
#include "libminiaudio-audio.h"

// Python module wrapper for AMY commands

static PyObject * send_wrapper(PyObject *self, PyObject *args) {
    char *arg1;
    if (! PyArg_ParseTuple(args, "s", &arg1)) {
        return NULL;
    }
    amy_play_message(arg1);
    return Py_None;
}


static PyObject * live_wrapper(PyObject *self, PyObject *args) {
    int arg1 = -1; int arg2 = -1;
    if(! PyArg_ParseTuple(args, "ii", &arg1, &arg2)) {
        amy_playback_device_id = -1;
        amy_capture_device_id = -1;
    } else {
        amy_playback_device_id = arg1;
        amy_capture_device_id = arg2;
    }
    amy_live_start();
    return Py_None;
}

static PyObject * pause_wrapper(PyObject *self, PyObject *args) {
    amy_live_stop();
    return Py_None;
}

static PyObject * restart_wrapper(PyObject *self, PyObject *args) {
    amy_stop();
    amy_start(/* cores= */ 1, /* reverb= */ 1, /* chorus= */ 1, /* echo= */ 1);
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


static PyMethodDef libAMYMethods[] = {
    {"render", render_wrapper, METH_VARARGS, "Render audio"},
    {"send", send_wrapper, METH_VARARGS, "Send a message"},
    {"live", live_wrapper, METH_VARARGS, "Live AMY"},
    {"pause", pause_wrapper, METH_VARARGS, "Pause AMY"},
    {"restart", restart_wrapper, METH_VARARGS, "Restart AMY"},
    {"config", config_wrapper, METH_VARARGS, "Return config"},
    { NULL, NULL, 0, NULL }
};

static struct PyModuleDef libamyDef =
{
    PyModuleDef_HEAD_INIT,
    "libamy", /* name of module */
    "",          /* module documentation, may be NULL */
    -1,          /* size of per-interpreter state of the module, or -1 if the module keeps state in global variables. */
    libAMYMethods
};

PyMODINIT_FUNC PyInit_libamy(void)
{   
    amy_start(/* cores= */ 1, /* reverb= */ 1, /* chorus= */ 1, /* echo= */ 1);
    return PyModule_Create(&libamyDef);

}
#endif
