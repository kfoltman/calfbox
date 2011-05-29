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

static PyObject *cbox_python_do_cmd(PyObject *self, PyObject *args)
{
    const char *command = NULL;
    const char *ret_path = NULL;
    PyObject *list = NULL;
    if (!PyArg_ParseTuple(args, "szO!:do_cmd", &command, &ret_path, &PyList_Type, &list))
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
    // cbox_osc_command_dump(&cmd);
    app.cmd_target.process_cmd(&app.cmd_target, NULL, &cmd, &error);
    free(arg_space);
    free(arg_values);
    free(arg_types);
    
    Py_RETURN_NONE;
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
