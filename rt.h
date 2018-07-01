/*
Calf Box, an open source musical instrument.
Copyright (C) 2010-2013 Krzysztof Foltman

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

#ifndef CBOX_RT_H
#define CBOX_RT_H

#include <stdint.h>

#include "cmd.h"
#include "dom.h"
#include "fifo.h"
#include "ioenv.h"
#include "midi.h"
#include "mididest.h"

#define RT_CMD_QUEUE_ITEMS 1024
#define RT_MAX_COST_PER_CALL 100

struct cbox_instruments;
struct cbox_scene;
struct cbox_io;
struct cbox_midi_pattern;
struct cbox_song;
struct cbox_rt_cmd_instance;

struct cbox_rt_cmd_definition
{
    int (*prepare)(void *user_data); // non-zero to skip the whole thing
    int (*execute)(void *user_data); // returns cost
    void (*cleanup)(void *user_data);
};

CBOX_EXTERN_CLASS(cbox_rt)

struct cbox_rt
{
    CBOX_OBJECT_HEADER()
    
    struct cbox_io *io;
    struct cbox_io_callbacks *cbs;
    
    struct cbox_fifo *rb_execute, *rb_cleanup;
    
    struct cbox_command_target cmd_target;
    int started, disconnected;
    struct cbox_io_env io_env;
    struct cbox_engine *engine;
};

extern struct cbox_rt *cbox_rt_new(struct cbox_document *doc);

extern void cbox_rt_set_io(struct cbox_rt *rt, struct cbox_io *io);
extern void cbox_rt_set_offline(struct cbox_rt *rt, int sample_rate, int buffer_size);
extern void cbox_rt_start(struct cbox_rt *rt, struct cbox_command_target *fb);
extern void cbox_rt_handle_cmd_queue(struct cbox_rt *rt);
// This one should be called from RT thread process function to execute the queued RT commands
extern void cbox_rt_handle_rt_commands(struct cbox_rt *rt);
extern void cbox_rt_stop(struct cbox_rt *rt);

// Those are for calling from the main thread. I will add a RT-thread version later.
extern void cbox_rt_execute_cmd_sync(struct cbox_rt *rt, struct cbox_rt_cmd_definition *cmd, void *user_data);
extern int cbox_rt_execute_cmd_async(struct cbox_rt *rt, struct cbox_rt_cmd_definition *cmd, void *user_data);
extern void *cbox_rt_swap_pointers(struct cbox_rt *rt, void **ptr, void *new_value);
extern void cbox_rt_swap_pointers_into(struct cbox_rt *rt, void **ptr, void *new_value, void **old_value_ptr);
extern void *cbox_rt_swap_pointers_and_update_count(struct cbox_rt *rt, void **ptr, void *new_value, int *pcount, int new_count);

extern void cbox_rt_array_insert(struct cbox_rt *rt, void ***ptr, int *pcount, int index, void *new_value);
extern void *cbox_rt_array_remove(struct cbox_rt *rt, void ***ptr, int *pcount, int index);
extern gboolean cbox_rt_array_remove_by_value(struct cbox_rt *rt, void ***ptr, int *pcount, void *value_to_remove);
extern struct cbox_midi_merger *cbox_rt_get_midi_output(struct cbox_rt *rt, struct cbox_uuid *uuid);

///////////////////////////////////////////////////////////////////////////////

#define GET_RT_FROM_cbox_rt(ptr) (ptr)
#define RT_FUNC_ARG_MEMBER(type, name) type name;
#define RT_FUNC_ARG_LIST(type, name) , type name
#define RT_FUNC_ARG_PASS(type, name) _args.name = name;
#define RT_FUNC_ARG_PASS2(type, name) , _args.name
#define RT_FUNC_ARG_PASS3(type, name) , _args->name
#define RT_FUNC_ARG_PASS4(type, name) , name
#define RT_FUNC_ARG_PASS5(type, name) _args->name = name;
#define DEFINE_RT_FUNC(restype, objtype, argname, name) \
    struct rt_function_args_##name { \
        struct objtype *_obj; \
        restype _result; \
        name##_args(RT_FUNC_ARG_MEMBER) \
    }; \
    static restype RT_IMPL_##name(struct objtype *_obj, int *_cost name##_args(RT_FUNC_ARG_LIST)); \
    int exec_##name(void *user_data) { \
        int cost = 1; \
        struct rt_function_args_##name *_args = user_data;\
        _args->_result = RT_IMPL_##name(_args->_obj, &cost name##_args(RT_FUNC_ARG_PASS3)); \
        return cost; \
    } \
    restype name(struct objtype *_obj name##_args(RT_FUNC_ARG_LIST)) \
    { \
        struct cbox_rt *rt = GET_RT_FROM_##objtype(_obj); \
        if (rt) { \
            struct rt_function_args_##name _args; \
            static struct cbox_rt_cmd_definition _cmd = { .prepare = NULL, .execute = exec_##name, .cleanup = NULL }; \
            _args._obj = _obj; \
            name##_args(RT_FUNC_ARG_PASS) \
            cbox_rt_execute_cmd_sync(rt, &_cmd, &_args);  \
            return _args._result; \
        } else { \
            int cost; \
            restype _result; \
            do { \
                cost = 1; \
                _result = RT_IMPL_##name(_obj, &cost name##_args(RT_FUNC_ARG_PASS4)); \
            } while(!cost); \
            return _result; \
        } \
    } \
    restype RT_IMPL_##name(struct objtype *argname, int *_cost name##_args(RT_FUNC_ARG_LIST))

#define DEFINE_RT_VOID_FUNC(objtype, argname, name) \
    struct rt_function_args_##name { \
        struct objtype *_obj; \
        name##_args(RT_FUNC_ARG_MEMBER) \
    }; \
    static void RT_IMPL_##name(struct objtype *_obj, int *_cost name##_args(RT_FUNC_ARG_LIST)); \
    int exec_##name(void *user_data) { \
        int cost = 1; \
        struct rt_function_args_##name *_args = user_data;\
        RT_IMPL_##name(_args->_obj, &cost name##_args(RT_FUNC_ARG_PASS3)); \
        return cost; \
    } \
    void name(struct objtype *_obj name##_args(RT_FUNC_ARG_LIST)) \
    { \
        struct cbox_rt *rt = GET_RT_FROM_##objtype(_obj); \
        if (rt) { \
            struct rt_function_args_##name _args = { ._obj = _obj }; \
            static struct cbox_rt_cmd_definition _cmd = { .prepare = NULL, .execute = exec_##name, .cleanup = NULL }; \
            name##_args(RT_FUNC_ARG_PASS) \
            cbox_rt_execute_cmd_sync(rt, &_cmd, &_args);  \
        } else { \
            int cost; \
            do { \
                cost = 1; \
                RT_IMPL_##name(_obj, &cost name##_args(RT_FUNC_ARG_PASS4)); \
            } while(!cost); \
        } \
    } \
    void RT_IMPL_##name(struct objtype *argname, int *_cost name##_args(RT_FUNC_ARG_LIST))

#define DEFINE_ASYNC_RT_FUNC(objtype, argname, name) \
    struct rt_function_args_##name { \
        struct objtype *_obj; \
        name##_args(RT_FUNC_ARG_MEMBER) \
    }; \
    static void RT_IMPL_##name(struct objtype *_obj, int *_cost name##_args(RT_FUNC_ARG_LIST)); \
    static int prepare_##name(void *user_data); \
    static void cleanup_##name(void *user_data); \
    static int exec_##name(void *user_data) { \
        int cost = 1; \
        struct rt_function_args_##name *_args = user_data;\
        RT_IMPL_##name(_args->_obj, &cost name##_args(RT_FUNC_ARG_PASS3)); \
        return cost; \
    } \
    void name(struct objtype *_obj name##_args(RT_FUNC_ARG_LIST)) \
    { \
        struct cbox_rt *rt = GET_RT_FROM_##objtype(_obj); \
        struct rt_function_args_##name *_args = malloc(sizeof(struct rt_function_args_##name)); \
        _args->_obj = _obj; \
        name##_args(RT_FUNC_ARG_PASS5) \
        static struct cbox_rt_cmd_definition _cmd = { .prepare = prepare_##name, .execute = exec_##name, .cleanup = cleanup_##name }; \
        if (cbox_rt_execute_cmd_async(rt, &_cmd, _args))  \
            free(_args);  \
    } \
    void RT_IMPL_##name(struct objtype *argname, int *_cost name##_args(RT_FUNC_ARG_LIST))

#define ASYNC_PREPARE_FUNC(objtype, argname, name) \
    static int PREPARE_IMPL_##name(struct objtype *argname, struct rt_function_args_##name *args); \
    int prepare_##name(void *user_data) {\
        struct rt_function_args_##name *_args = user_data;\
        return PREPARE_IMPL_##name(_args->_obj, _args); \
    } \
    int PREPARE_IMPL_##name(struct objtype *argname, struct rt_function_args_##name *args)

#define ASYNC_CLEANUP_FUNC(objtype, argname, name) \
    static void CLEANUP_IMPL_##name(struct objtype *argname, struct rt_function_args_##name *args); \
    void cleanup_##name(void *user_data) {\
        struct rt_function_args_##name *_args = user_data;\
        CLEANUP_IMPL_##name(_args->_obj, _args); \
        free(_args); \
    } \
    void CLEANUP_IMPL_##name(struct objtype *argname, struct rt_function_args_##name *args)

#define RT_CALL_AGAIN_LATER() ((*_cost) = 0)
#define RT_SET_COST(n) ((*_cost) = (n))

#endif
