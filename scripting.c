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

#include "app.h"
#include "errors.h"
#include "scripting.h"
#include <assert.h>
#include <glib.h>
#include <Python.h>

static PyObject *cbox_python_do_cmd_on(struct cbox_command_target *ct, PyObject *self, PyObject *args);

static gboolean bridge_to_python_callback(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    PyObject *callback = ct->user_data;
    
    int argc = strlen(cmd->arg_types);
    PyObject *arg_values = PyList_New(argc);
    for (int i = 0; i < argc; i++)
    {
        if (cmd->arg_types[i] == 's')
        {
            PyList_SetItem(arg_values, i, PyString_FromString(cmd->arg_values[i]));
        }
        else
        if (cmd->arg_types[i] == 'i')
        {
            PyList_SetItem(arg_values, i, PyInt_FromLong(*(int *)cmd->arg_values[i]));
        }
        else
        if (cmd->arg_types[i] == 'f')
        {
            PyList_SetItem(arg_values, i, PyFloat_FromDouble(*(double *)cmd->arg_values[i]));
        }
        else
        {
            PyList_SetItem(arg_values, i, Py_None);
            Py_INCREF(Py_None);
        }
    }
    PyObject *args = PyTuple_New(3);
    PyTuple_SetItem(args, 0, PyString_FromString(cmd->command));
    PyTuple_SetItem(args, 1, Py_None); // XXXKF
    PyTuple_SetItem(args, 2, arg_values);
    Py_INCREF(Py_None);
//    PyTuple_SetItem(args, 2, PyList_New(strlen(cmd->arg_types)));
    
    PyObject_Call(callback, args, NULL);
    
    return TRUE;
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
    void **arg_values = malloc(len * sizeof(void *));
    cmd.command = command;
    cmd.arg_types = arg_types;
    cmd.arg_values = arg_values;
    double *arg_space = extra;
    for (int i = 0; i < len; i++)
    {
        cmd.arg_values[i] = &arg_space[i];
        PyObject *value = PyList_GetItem(list, i);
        
        if (PyInt_Check(value))
        {
            arg_types[i] = 'i';
            *(int *)arg_values[i] = PyInt_AsLong(value);
        }
        else
        if (PyFloat_Check(value))
        {
            arg_types[i] = 'f';
            *(double *)arg_values[i] = PyFloat_AsDouble(value);
        }
        else
        if (PyString_Check(value))
        {
            arg_types[i] = 's';
            arg_values[i] = PyString_AsString(value);
        }
        else
            assert(0);
    }
    arg_types[len] = '\0';
    
    struct cbox_command_target target;
    target.user_data = callback;
    target.process_cmd = bridge_to_python_callback;
    
    // cbox_osc_command_dump(&cmd);
    gboolean result = ct->process_cmd(ct, callback != Py_None ? &target : NULL, &cmd, &error);
    free(arg_space);
    free(arg_values);
    free(arg_types);
    
    if (!result)
    {
        return PyErr_Format(PyExc_Exception, "%s", error ? error->message : "Unknown error");
    }
    
    Py_RETURN_NONE;
}

static PyObject *cbox_python_do_cmd(PyObject *self, PyObject *args)
{
    return cbox_python_do_cmd_on(&app.cmd_target, self, args);
}

static PyMethodDef EmbMethods[] = {
    {"do_cmd", cbox_python_do_cmd, METH_VARARGS, "Execute a CalfBox command using a global path."},
    {NULL, NULL, 0, NULL}
};

void cbox_script_run(const char *name)
{
    FILE *fp = fopen(name, "rb");
    if (!fp)
    {
        g_warning("Cannot open script file '%s': %s", name, strerror(errno));
        return;
    }
    Py_Initialize();
    Py_InitModule("cbox", EmbMethods);
    PyRun_SimpleFile(fp, name);
    Py_Finalize();
}
