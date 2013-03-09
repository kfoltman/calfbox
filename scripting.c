/*
Calf Box, an open source musical instrument.
Copyright (C) 2010-2011 Krzysztof Foltman

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "config.h"

#if USE_PYTHON

#include "app.h"
#include "blob.h"
#include "errors.h"
#include "scripting.h"
#include <assert.h>
#include <glib.h>

// This is a workaround for what I consider a defect in pyconfig.h
#undef _XOPEN_SOURCE
#undef _POSIX_C_SOURCE

#include <Python.h>

static gboolean engine_initialised = FALSE;

struct PyCboxCallback 
{
    PyObject_HEAD
    struct cbox_command_target *target;
};

static PyObject *
PyCboxCallback_New(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    struct PyCboxCallback *self;

    self = (struct PyCboxCallback *)type->tp_alloc(type, 0);
    if (self != NULL) {
        self->target = NULL;
    }

    return (PyObject *)self;
}

static int
PyCboxCallback_Init(struct PyCboxCallback *self, PyObject *args, PyObject *kwds)
{
    PyObject *cobj = NULL;
    if (!PyArg_ParseTuple(args, "O!:init", &PyCapsule_Type, &cobj))
        return -1;
    
    self->target = PyCapsule_GetPointer(cobj, NULL);
    return 0;
}

static PyObject *cbox_python_do_cmd_on(struct cbox_command_target *ct, PyObject *self, PyObject *args);

static PyObject *
PyCboxCallback_Call(PyObject *_self, PyObject *args, PyObject *kwds)
{
    struct PyCboxCallback *self = (struct PyCboxCallback *)_self;

    return cbox_python_do_cmd_on(self->target, _self, args);
}

PyTypeObject CboxCallbackType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "_cbox.Callback",
    .tp_basicsize = sizeof(struct PyCboxCallback),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = "Callback for feedback channel to Cbox C code",
    .tp_init = (initproc)PyCboxCallback_Init,
    .tp_new = PyCboxCallback_New,
    .tp_call = PyCboxCallback_Call
};

static gboolean set_error_from_python(GError **error)
{
    PyObject *ptype = NULL, *pvalue = NULL, *ptraceback = NULL;
    PyErr_Fetch(&ptype, &pvalue, &ptraceback);
    PyObject *ptypestr = PyObject_Str(ptype);
    PyObject *pvaluestr = PyObject_Str(pvalue);
    PyObject *ptypestr_unicode = PyUnicode_AsUTF8String(ptypestr);
    PyObject *pvaluestr_unicode = PyUnicode_AsUTF8String(pvaluestr);
    g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "%s: %s", PyBytes_AsString(ptypestr_unicode), PyBytes_AsString(pvaluestr_unicode));
    Py_DECREF(pvaluestr_unicode);
    Py_DECREF(ptypestr_unicode);
    //g_error("%s:%s", PyString_AsString(ptypestr), PyString_AsString(pvaluestr));
    Py_DECREF(ptypestr);
    Py_DECREF(pvaluestr);
    Py_DECREF(ptype);
    Py_XDECREF(pvalue);
    Py_XDECREF(ptraceback);
    return FALSE;
}

static gboolean bridge_to_python_callback(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    PyObject *callback = ct->user_data;
    
    int argc = strlen(cmd->arg_types);
    PyObject *arg_values = PyList_New(argc);
    for (int i = 0; i < argc; i++)
    {
        if (cmd->arg_types[i] == 's')
        {
            PyList_SetItem(arg_values, i, PyUnicode_FromString(cmd->arg_values[i]));
        }
        else
        if (cmd->arg_types[i] == 'o')
        {
            struct cbox_objhdr *oh = cmd->arg_values[i];
            char buf[40];
            uuid_unparse(oh->instance_uuid.uuid, buf);
            PyList_SetItem(arg_values, i, PyUnicode_FromString(buf));
        }
        else
        if (cmd->arg_types[i] == 'i')
        {
            PyList_SetItem(arg_values, i, PyLong_FromLong(*(int *)cmd->arg_values[i]));
        }
        else
        if (cmd->arg_types[i] == 'f')
        {
            PyList_SetItem(arg_values, i, PyFloat_FromDouble(*(double *)cmd->arg_values[i]));
        }
        else
        if (cmd->arg_types[i] == 'b')
        {
            struct cbox_blob *blob = cmd->arg_values[i];
            PyList_SetItem(arg_values, i, PyByteArray_FromStringAndSize(blob->data, blob->size));
        }
        else
        {
            PyList_SetItem(arg_values, i, Py_None);
            Py_INCREF(Py_None);
        }
    }
    struct PyCboxCallback *fbcb = NULL;
    
    PyObject *args = PyTuple_New(3);
    PyTuple_SetItem(args, 0, PyUnicode_FromString(cmd->command));
    PyObject *pyfb = NULL;
    if (fb)
    {
        struct PyCboxCallback *fbcb = PyObject_New(struct PyCboxCallback, &CboxCallbackType);
        fbcb->target = fb;
        pyfb = (PyObject *)fbcb;
    }
    else
    {
        pyfb = Py_None;
        Py_INCREF(Py_None);
    }
    PyTuple_SetItem(args, 1, pyfb);
    PyTuple_SetItem(args, 2, arg_values);
    
    PyObject *result = PyObject_Call(callback, args, NULL);
    Py_DECREF(args);
    
    if (fbcb)
        fbcb->target = NULL;
    
    if (result)
    {
        Py_DECREF(result);
        return TRUE;
    }
    
    return set_error_from_python(error);
}

static PyObject *cbox_python_do_cmd_on(struct cbox_command_target *ct, PyObject *self, PyObject *args)
{
    const char *command = NULL;
    PyObject *callback = NULL;
    PyObject *list = NULL;
    if (!PyArg_ParseTuple(args, "sOO!:do_cmd", &command, &callback, &PyList_Type, &list))
        return NULL;
    
    int len = PyList_Size(list);
    void *extra = malloc(len * sizeof(double));
    struct cbox_osc_command cmd;
    GError *error = NULL;
    char *arg_types = malloc(len + 1);
    void **arg_values = malloc(2 * len * sizeof(void *));
    void **arg_extra = &arg_values[len];
    cmd.command = command;
    cmd.arg_types = arg_types;
    cmd.arg_values = arg_values;
    double *arg_space = extra;
    gboolean free_blobs = FALSE;
    for (int i = 0; i < len; i++)
    {
        cmd.arg_values[i] = &arg_space[i];
        PyObject *value = PyList_GetItem(list, i);
        
        if (PyLong_Check(value))
        {
            arg_types[i] = 'i';
            *(int *)arg_values[i] = PyLong_AsLong(value);
        }
        else
        if (PyFloat_Check(value))
        {
            arg_types[i] = 'f';
            *(double *)arg_values[i] = PyFloat_AsDouble(value);
        }
        else
        if (PyUnicode_Check(value))
        {
            PyObject *utf8str = PyUnicode_AsUTF8String(value);
            arg_types[i] = 's';
            arg_extra[i] = utf8str;
            arg_values[i] = PyBytes_AsString(utf8str);
        }
        else
        if (PyByteArray_Check(value))
        {
            const void *buf = PyByteArray_AsString(value);
            ssize_t len = PyByteArray_Size(value);
            
            if (buf)
            {
                // note: this is not really acquired, the blob is freed using free and not cbox_blob_destroy
                struct cbox_blob *blob = cbox_blob_new_acquire_data((void *)buf, len);
                arg_types[i] = 'b';
                arg_values[i] = blob;
                free_blobs = TRUE;
            }
            else
                arg_types[i] = 'N';
        }
        else
        {
            PyObject *ob_type = (PyObject *)value->ob_type;
            PyObject *typename_unicode = PyObject_Str(ob_type);
            PyObject *typename_bytes = PyUnicode_AsUTF8String(typename_unicode);
            g_error("Cannot serialize Python type '%s'", PyBytes_AsString(typename_bytes));
            Py_DECREF(typename_bytes);
            Py_DECREF(typename_unicode);
            
            assert(0);
        }
    }
    arg_types[len] = '\0';
    
    struct cbox_command_target target;
    cbox_command_target_init(&target, bridge_to_python_callback, callback);
    
    // cbox_osc_command_dump(&cmd);
    Py_INCREF(callback);
    gboolean result = ct->process_cmd(ct, callback != Py_None ? &target : NULL, &cmd, &error);
    Py_DECREF(callback);
    
    if (free_blobs)
    {
        for (int i = 0; i < len; i++)
        {
            if (arg_types[i] == 'b')
                free(arg_values[i]);
            if (arg_types[i] == 's')
                Py_DECREF((PyObject *)arg_extra[i]);
        }
    }
    free(arg_space);
    free(arg_values);
    free(arg_types);
    
    if (!result)
        return PyErr_Format(PyExc_Exception, "%s", error ? error->message : "Unknown error");
    
    Py_RETURN_NONE;
}

static PyObject *cbox_python_do_cmd(PyObject *self, PyObject *args)
{
    if (!engine_initialised)
        return PyErr_Format(PyExc_Exception, "Engine not initialised");
    return cbox_python_do_cmd_on(&app.cmd_target, self, args);
}

#if CALFBOX_AS_MODULE

#include "config-api.h"
#include "wavebank.h"
#include "scene.h"

static gboolean audio_running = FALSE;

static PyObject *cbox_python_init_engine(PyObject *self, PyObject *args)
{
    const char *config_file = NULL;
    if (!PyArg_ParseTuple(args, "|z:init_engine", &config_file))
        return NULL;
    if (engine_initialised)
        return PyErr_Format(PyExc_Exception, "Engine already initialised");
 
    cbox_dom_init();
    app.document = cbox_document_new();
    app.rt = cbox_rt_new(app.document);    
    cbox_config_init(config_file);
    cbox_wavebank_init();
    engine_initialised = 1;

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *cbox_python_shutdown_engine(PyObject *self, PyObject *args)
{
    if (!PyArg_ParseTuple(args, ":shutdown_engine"))
        return NULL;
    if (!engine_initialised)
        return PyErr_Format(PyExc_Exception, "Engine not initialised");
    
    cbox_rt_destroy(app.rt);
    cbox_wavebank_close();
    cbox_config_close();
    cbox_dom_close();
    engine_initialised = FALSE;
    
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *cbox_python_start_audio(PyObject *self, PyObject *args)
{
    PyObject *callback = NULL;
    if (!PyArg_ParseTuple(args, "|O:start_audio", &callback))
        return NULL;
    if (!engine_initialised)
        return PyErr_Format(PyExc_Exception, "Engine not initialised");
    if (audio_running)
        return PyErr_Format(PyExc_Exception, "Audio already started");

    struct cbox_open_params params;
    GError *error = NULL;
    
    struct cbox_command_target target;
    if (callback && callback != Py_None)
        cbox_command_target_init(&target, bridge_to_python_callback, callback);

    if (!cbox_io_init(&app.io, &params, (callback && callback != Py_None) ? &target : NULL, &error))
        return PyErr_Format(PyExc_IOError, "Cannot initialise sound I/O: %s", (error && error->message) ? error->message : "Unknown error");

    cbox_rt_set_io(app.rt, &app.io);
    cbox_rt_set_scene(app.rt, cbox_scene_new(app.document, app.rt, FALSE));
    cbox_rt_start(app.rt, (callback && callback != Py_None) ? &target : NULL);
    audio_running = TRUE;

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *cbox_python_start_noaudio(PyObject *self, PyObject *args)
{
    PyObject *callback = NULL;
    int sample_rate = 0;
    if (!PyArg_ParseTuple(args, "i|O:start_noaudio", &sample_rate, &callback))
        return NULL;
    if (!engine_initialised)
        return PyErr_Format(PyExc_Exception, "Engine not initialised");
    if (audio_running)
        return PyErr_Format(PyExc_Exception, "Audio already started");

    struct cbox_command_target target;
    if (callback && callback != Py_None)
        cbox_command_target_init(&target, bridge_to_python_callback, callback);

    cbox_rt_set_offline(app.rt, sample_rate, 1024);
    cbox_rt_set_scene(app.rt, cbox_scene_new(app.document, app.rt, FALSE));
    cbox_rt_start(app.rt, (callback && callback != Py_None) ? &target : NULL);
    audio_running = TRUE;

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *cbox_python_stop_audio(PyObject *self, PyObject *args)
{
    if (!PyArg_ParseTuple(args, ":stop_audio"))
        return NULL;
    if (!engine_initialised)
        return PyErr_Format(PyExc_Exception, "Engine not initialised");
    if (!audio_running)
        return PyErr_Format(PyExc_Exception, "Audio not running");

    cbox_rt_set_scene(app.rt, NULL);
    cbox_rt_stop(app.rt);
    cbox_io_close(&app.io);
    audio_running = FALSE;
    Py_INCREF(Py_None);
    return Py_None;
}

#endif

static PyMethodDef CboxMethods[] = {
    {"do_cmd", cbox_python_do_cmd, METH_VARARGS, "Execute a CalfBox command using a global path."},
#if CALFBOX_AS_MODULE
    {"init_engine", cbox_python_init_engine, METH_VARARGS, "Initialise the CalfBox engine using optional config file."},
    {"shutdown_engine", cbox_python_shutdown_engine, METH_VARARGS, "Shutdown the CalfBox engine."},
    {"start_audio", cbox_python_start_audio, METH_VARARGS, "Start real-time audio processing using I/O settings from the current config."},
    {"start_noaudio", cbox_python_start_noaudio, METH_VARARGS, "Start dummy audio processing using sample rate specified as argument."},
    {"stop_audio", cbox_python_stop_audio, METH_VARARGS, "Stop real-time audio processing."},
#endif    
    {NULL, NULL, 0, NULL}
};

static PyModuleDef CboxModule = {
    PyModuleDef_HEAD_INIT, "_cbox", NULL, -1, CboxMethods,
    NULL, NULL, NULL, NULL
};

#if CALFBOX_AS_MODULE

PyMODINIT_FUNC
PyInit__cbox(void)
{    
    PyObject *m = PyModule_Create(&CboxModule);
    if (!m)
        return NULL;
    Py_INCREF(&CboxCallbackType);
    if (PyType_Ready(&CboxCallbackType) < 0)
        return NULL;
    PyModule_AddObject(m, "Callback", (PyObject *)&CboxCallbackType);
    
    return m;
}

#else

PyObject*
PyInit_cbox(void)
{
    PyObject *m = PyModule_Create(&CboxModule);
    PyModule_AddObject(m, "Callback", (PyObject *)&CboxCallbackType);
    return m;
}

void cbox_script_run(const char *name)
{
    FILE *fp = fopen(name, "rb");
    if (!fp)
    {
        g_warning("Cannot open script file '%s': %s", name, strerror(errno));
        return;
    }
    PyImport_AppendInittab("_cbox", &PyInit_cbox);
    Py_Initialize();
    if (PyType_Ready(&CboxCallbackType) < 0)
    {
        g_warning("Cannot install the C callback type");
        return;
    }
    Py_INCREF(&CboxCallbackType);
    engine_initialised = TRUE;
    
    if (PyRun_SimpleFile(fp, name) == 1)
    {
        GError *error = NULL;
        set_error_from_python(&error);
        cbox_print_error(error);
    }
    Py_Finalize();
}

#endif
#endif
