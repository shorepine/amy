#include <Python.h>
#include <math.h>
#include "amy.h"

// Python module wrapper for AMY commands

static PyObject * send_wrapper(PyObject *self, PyObject *args) {
    char *arg1;
    if (! PyArg_ParseTuple(args, "s", &arg1)) {
        return NULL;
    }
    parse_message(arg1);
    return Py_None;
}

uint8_t started;
static PyObject * start_wrapper(PyObject *self, PyObject *args) {
    if(started==0) {
        start_amy();
        started = 1;
    } else {
        printf("Already started\n");
    }
    return Py_None;
}

static PyObject * live_wrapper(PyObject *self, PyObject *args) {
    if(started==1) {
        live_start();
    } else {
        printf("Not yet started\n");
    }
    return Py_None;
}

static PyObject * pause_wrapper(PyObject *self, PyObject *args) {
    live_stop();
    return Py_None;
}


static PyObject * stop_wrapper(PyObject *self, PyObject *args) {
    if(started == 1) {
        stop_amy();
        started = 0;
    } else {
        printf("Already stopped.\n");
    }
    return Py_None;
}

static PyObject * render_wrapper(PyObject *self, PyObject *args) {
    if(started) {
        int16_t * result = fill_audio_buffer_task();
        // Create a python list of ints (they are signed shorts that come back)
        PyObject* ret = PyList_New(BLOCK_SIZE); 
        for (int i = 0; i < BLOCK_SIZE; i++) {
            PyObject* python_int = Py_BuildValue("i", result[i]);
            PyList_SetItem(ret, i, python_int);
        }
        return ret;
    } else {
        printf("Not started, can't render. call libamy.start() first.\n");
        return Py_None;
    }
}


static PyMethodDef libAMYMethods[] = {
    {"render", render_wrapper, METH_VARARGS, "Render audio"},
    {"send", send_wrapper, METH_VARARGS, "Send a message"},
    {"start", start_wrapper, METH_VARARGS, "Start AMY"},
    {"stop", stop_wrapper, METH_VARARGS, "Stop AMY"},
    {"live", live_wrapper, METH_VARARGS, "Live AMY"},
    {"pause", pause_wrapper, METH_VARARGS, "Pause AMY"},
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
    started=0;
    return PyModule_Create(&libamyDef);

}
