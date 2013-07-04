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

#include "auxbus.h"
#include "config-api.h"
#include "engine.h"
#include "errors.h"
#include "instr.h"
#include "io.h"
#include "layer.h"
#include "master.h"
#include "midi.h"
#include "module.h"
#include "pattern.h"
#include "rt.h"
#include "scene.h"
#include "seq.h"
#include <assert.h>
#include <glib.h>

CBOX_CLASS_DEFINITION_ROOT(cbox_scene)

static gboolean cbox_scene_addlayercmd(struct cbox_scene *s, struct cbox_command_target *fb, struct cbox_osc_command *cmd, int cmd_type, GError **error)
{
    int pos = CBOX_ARG_I(cmd, 0);
    if (pos < 0 || pos > 1 + s->layer_count)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Invalid position %d (valid are 1..%d or 0 for append)", pos, 1 + s->layer_count);
        return FALSE;
    }
    if (pos == 0)
        pos = s->layer_count;
    else
        pos--;
    struct cbox_layer *layer = NULL;
    
    switch(cmd_type)
    {
    case 1:
        layer = cbox_layer_new_from_config(s, CBOX_ARG_S(cmd, 1), error);
        break;
    case 2:
        layer = cbox_layer_new_with_instrument(s, CBOX_ARG_S(cmd, 1), error);
        break;
    case 3:
        {
            struct cbox_instrument *instr = cbox_scene_create_instrument(s, CBOX_ARG_S(cmd, 1), CBOX_ARG_S(cmd, 2), error);
            if (!instr)
                return FALSE;
            layer = cbox_layer_new_with_instrument(s, CBOX_ARG_S(cmd, 1), error);
            break;
        }
    default:
        assert(0);
        break;
    }
    if (!layer)
        return FALSE;
    if (!cbox_scene_insert_layer(s, layer, pos, error))
    {
        CBOX_DELETE(layer);
        return FALSE;
    }
    if (fb)
    {
        if (!cbox_execute_on(fb, NULL, "/uuid", "o", error, layer))
            return FALSE;
    }
    return TRUE;
}

static gboolean cbox_scene_process_cmd(struct cbox_command_target *ct, struct cbox_command_target *fb, struct cbox_osc_command *cmd, GError **error)
{
    struct cbox_scene *s = ct->user_data;
    const char *subcommand = NULL;
    char *subobj = NULL;
    int index = 0;
    
    if (!strcmp(cmd->command, "/transpose") && !strcmp(cmd->arg_types, "i"))
    {
        s->transpose = CBOX_ARG_I(cmd, 0);
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/load") && !strcmp(cmd->arg_types, "s"))
    {
        if (!cbox_scene_load(s, CBOX_ARG_S(cmd, 0), error))
            return FALSE;
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/clear") && !strcmp(cmd->arg_types, ""))
    {
        cbox_scene_clear(s);
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/add_layer") && !strcmp(cmd->arg_types, "is"))
    {
        return cbox_scene_addlayercmd(s, fb, cmd, 1, error);
    }
    else if (!strcmp(cmd->command, "/add_instrument_layer") && !strcmp(cmd->arg_types, "is"))
    {
        return cbox_scene_addlayercmd(s, fb, cmd, 2, error);
    }
    else if (!strcmp(cmd->command, "/add_new_instrument_layer") && !strcmp(cmd->arg_types, "iss"))
    {
        return cbox_scene_addlayercmd(s, fb, cmd, 3, error);
    }
    else if (!strcmp(cmd->command, "/delete_layer") && !strcmp(cmd->arg_types, "i"))
    {
        int pos = CBOX_ARG_I(cmd, 0);
        if (pos < 0 || pos > s->layer_count)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Invalid position %d (valid are 1..%d or 0 for last)", pos, s->layer_count);
            return FALSE;
        }
        if (pos == 0)
            pos = s->layer_count - 1;
        else
            pos--;
        struct cbox_layer *layer = cbox_scene_remove_layer(s, pos);
        CBOX_DELETE(layer);
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/move_layer") && !strcmp(cmd->arg_types, "ii"))
    {
        int oldpos = CBOX_ARG_I(cmd, 0);
        if (oldpos < 1 || oldpos > s->layer_count)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Invalid position %d (valid are 1..%d)", oldpos, s->layer_count);
            return FALSE;
        }
        int newpos = CBOX_ARG_I(cmd, 1);
        if (newpos < 1 || newpos > s->layer_count)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Invalid position %d (valid are 1..%d)", newpos, s->layer_count);
            return FALSE;
        }
        cbox_scene_move_layer(s, oldpos - 1, newpos - 1);
        return TRUE;
    }
    else if (cbox_parse_path_part_int(cmd, "/layer/", &subcommand, &index, 1, s->layer_count, error))
    {
        if (!subcommand)
            return FALSE;
        return cbox_execute_sub(&s->layers[index - 1]->cmd_target, fb, cmd, subcommand, error);
    }
    else if (cbox_parse_path_part_str(cmd, "/aux/", &subcommand, &subobj, error))
    {
        if (!subcommand)
            return FALSE;
        struct cbox_aux_bus *aux = cbox_scene_get_aux_bus(s, subobj, FALSE, error);
        g_free(subobj);
        if (!aux)
            return FALSE;
        return cbox_execute_sub(&aux->cmd_target, fb, cmd, subcommand, error);
    }
    else if (!strncmp(cmd->command, "/instr/", 7))
    {
        const char *obj = &cmd->command[1];
        const char *pos = strchr(obj, '/');
        obj = &pos[1];
        pos = strchr(obj, '/');
        if (!pos)
        {
            g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Invalid instrument path '%s'", cmd->command);
            return FALSE;
        }
        int len = pos - obj;
        
        gchar *name = g_strndup(obj, len);
        struct cbox_instrument *instr = cbox_scene_get_instrument_by_name(s, name, FALSE, error);
        if (instr)
        {
            g_free(name);
            
            return cbox_execute_sub(&instr->cmd_target, fb, cmd, pos, error);
        }
        else
        {
            cbox_force_error(error);
            g_prefix_error(error, "Cannot access instrument '%s': ", name);
            g_free(name);
            return FALSE;
        }
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/load_aux") && !strcmp(cmd->arg_types, "s"))
    {
        struct cbox_aux_bus *bus = cbox_scene_get_aux_bus(s, CBOX_ARG_S(cmd, 0), TRUE, error);
        if (!bus)
            return FALSE;
        if (fb)
        {
            if (!cbox_execute_on(fb, NULL, "/uuid", "o", error, bus))
                return FALSE;
        }
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/delete_aux") && !strcmp(cmd->arg_types, "s"))
    {
        const char *name = CBOX_ARG_S(cmd, 0);
        struct cbox_aux_bus *aux = cbox_scene_get_aux_bus(s, name, FALSE, error);
        if (!aux)
            return FALSE;
        CBOX_DELETE(aux);
        return TRUE;
    }
    else if (!strcmp(cmd->command, "/status") && !strcmp(cmd->arg_types, ""))
    {
        if (!cbox_check_fb_channel(fb, cmd->command, error))
            return FALSE;

        if (!cbox_execute_on(fb, NULL, "/name", "s", error, s->name) || 
            !cbox_execute_on(fb, NULL, "/title", "s", error, s->title) ||
            !cbox_execute_on(fb, NULL, "/transpose", "i", error, s->transpose) ||
            !cbox_execute_on(fb, NULL, "/enable_default_song_input", "i", error, s->enable_default_song_input) ||
            !cbox_execute_on(fb, NULL, "/enable_default_external_input", "i", error, s->enable_default_external_input) ||
            !CBOX_OBJECT_DEFAULT_STATUS(s, fb, error))
            return FALSE;
        
        for (int i = 0; i < s->layer_count; i++)
        {
            if (!cbox_execute_on(fb, NULL, "/layer", "o", error, s->layers[i]))
                return FALSE;
        }
        for (int i = 0; i < s->instrument_count; i++)
        {
            if (!cbox_execute_on(fb, NULL, "/instrument", "sso", error, s->instruments[i]->module->instance_name, s->instruments[i]->module->engine_name, s->instruments[i]))
                return FALSE;
        }
        for (int i = 0; i < s->aux_bus_count; i++)
        {
            if (!cbox_execute_on(fb, NULL, "/aux", "so", error, s->aux_buses[i]->name, s->aux_buses[i]))
                return FALSE;
        }
        return TRUE;
    }
    else
    if (!strcmp(cmd->command, "/send_event") && (!strcmp(cmd->arg_types, "iii") || !strcmp(cmd->arg_types, "ii") || !strcmp(cmd->arg_types, "i")))
    {
        int mcmd = CBOX_ARG_I(cmd, 0);
        int arg1 = 0, arg2 = 0;
        if (cmd->arg_types[1] == 'i')
        {
            arg1 = CBOX_ARG_I(cmd, 1);
            if (cmd->arg_types[2] == 'i')
                arg2 = CBOX_ARG_I(cmd, 2);
        }
        struct cbox_midi_buffer buf;
        cbox_midi_buffer_init(&buf);
        cbox_midi_buffer_write_inline(&buf, 0, mcmd, arg1, arg2);
        cbox_midi_merger_push(&s->scene_input_merger, &buf, s->rt);
        return TRUE;
    }
    else
    if (!strcmp(cmd->command, "/play_note") && !strcmp(cmd->arg_types, "iii"))
    {
        int channel = CBOX_ARG_I(cmd, 0);
        int note = CBOX_ARG_I(cmd, 1);
        int velocity = CBOX_ARG_I(cmd, 2);
        struct cbox_midi_buffer buf;
        cbox_midi_buffer_init(&buf);
        cbox_midi_buffer_write_inline(&buf, 0, 0x90 + ((channel - 1) & 15), note & 127, velocity & 127);
        cbox_midi_buffer_write_inline(&buf, 1, 0x80 + ((channel - 1) & 15), note & 127, velocity & 127);
        cbox_midi_merger_push(&s->scene_input_merger, &buf, s->rt);
        return TRUE;
    }
    else
    if (!strcmp(cmd->command, "/play_pattern") && !strcmp(cmd->arg_types, "sfi"))
    {
        struct cbox_midi_pattern *pattern = (struct cbox_midi_pattern *)CBOX_ARG_O(cmd, 0, s, cbox_midi_pattern, error);
        if (!pattern)
            return FALSE;
        
        struct cbox_adhoc_pattern *ap = cbox_adhoc_pattern_new(s->engine, CBOX_ARG_I(cmd, 2), pattern);
        ap->master->tempo = ap->master->new_tempo = CBOX_ARG_F(cmd, 1);
        cbox_scene_play_adhoc_pattern(s, ap);
        return TRUE;
    }
    else
    if (!strcmp(cmd->command, "/enable_default_song_input") && !strcmp(cmd->arg_types, "i"))
    {
        s->enable_default_song_input = CBOX_ARG_I(cmd, 0);
        return TRUE;
    }
    else
    if (!strcmp(cmd->command, "/enable_default_external_input") && !strcmp(cmd->arg_types, "i"))
    {
        s->enable_default_external_input = CBOX_ARG_I(cmd, 0);
        return TRUE;
    }
    else
        return cbox_object_default_process_cmd(ct, fb, cmd, error);
}

gboolean cbox_scene_load(struct cbox_scene *s, const char *name, GError **error)
{
    const char *cv = NULL;
    int i;
    gchar *section = g_strdup_printf("scene:%s", name);
    
    if (!cbox_config_has_section(section))
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "No config section for scene '%s'", name);
        goto error;
    }
    
    cbox_scene_clear(s);
    
    assert(s->layers == NULL);
    assert(s->instruments == NULL);
    assert(s->aux_buses == NULL);
    assert(s->layer_count == 0);
    assert(s->instrument_count == 0);
    assert(s->aux_bus_count == 0);
    
    for (i = 1; ; i++)
    {
        struct cbox_layer *l = NULL;
        
        gchar *sn = g_strdup_printf("layer%d", i);
        cv = cbox_config_get_string(section, sn);
        g_free(sn);
        
        if (!cv)
            break;
        
        l = cbox_layer_new_from_config(s, cv, error);
        if (!l)
            goto error;
        
        if (!cbox_scene_add_layer(s, l, error))
            goto error;
    }
    
    s->transpose = cbox_config_get_int(section, "transpose", 0);
    s->title = g_strdup(cbox_config_get_string_with_default(section, "title", ""));
    g_free(section);
    cbox_command_target_init(&s->cmd_target, cbox_scene_process_cmd, s);
    s->name = g_strdup(name);
    return TRUE;

error:
    g_free(section);
    return FALSE;
}

gboolean cbox_scene_insert_layer(struct cbox_scene *scene, struct cbox_layer *layer, int pos, GError **error)
{
    int i;
    
    struct cbox_instrument *instrument = layer->instrument;
    for (i = 0; i < instrument->aux_output_count; i++)
    {
        assert(!instrument->aux_outputs[i]);
        if (instrument->aux_output_names[i])
        {
            instrument->aux_outputs[i] = cbox_scene_get_aux_bus(scene, instrument->aux_output_names[i], TRUE, error);
            if (!instrument->aux_outputs[i])
                return FALSE;
            cbox_aux_bus_ref(instrument->aux_outputs[i]);
        }
    }
    for (i = 0; i < scene->layer_count; i++)
    {
        if (scene->layers[i]->instrument == layer->instrument)
            break;
    }
    if (i == scene->layer_count)
    {
        layer->instrument->scene = scene;
        cbox_rt_array_insert(scene->rt, (void ***)&scene->instruments, &scene->instrument_count, -1, layer->instrument);
    }
    cbox_rt_array_insert(scene->rt, (void ***)&scene->layers, &scene->layer_count, pos, layer);
    
    return TRUE;
}

gboolean cbox_scene_add_layer(struct cbox_scene *scene, struct cbox_layer *layer, GError **error)
{
    return cbox_scene_insert_layer(scene, layer, scene->layer_count, error);
}

struct cbox_layer *cbox_scene_remove_layer(struct cbox_scene *scene, int pos)
{
    struct cbox_layer *removed = scene->layers[pos];
    cbox_rt_array_remove(scene->rt, (void ***)&scene->layers, &scene->layer_count, pos);
    cbox_instrument_unref_aux_buses(removed->instrument);
    
    return removed;
}

void cbox_scene_move_layer(struct cbox_scene *scene, int oldpos, int newpos)
{
    if (oldpos == newpos)
        return;
    struct cbox_layer **layers = malloc(sizeof(struct cbox_layer *) * scene->layer_count);
    for (int i = 0; i < scene->layer_count; i++)
    {
        int s;
        if (i == newpos)
            s = oldpos;
        else
        {
            if (oldpos < newpos)
                s = (i < oldpos || i > newpos) ? i : i + 1;
            else
                s = (i < newpos || i > oldpos) ? i : i - 1;
        }
        layers[i] = scene->layers[s];
    }
    free(cbox_rt_swap_pointers(scene->rt, (void **)&scene->layers, layers));
}

gboolean cbox_scene_remove_instrument(struct cbox_scene *scene, struct cbox_instrument *instrument)
{
    assert(instrument->scene == scene);
    int pos;
    for (pos = 0; pos < scene->instrument_count; pos++)
    {
        if (scene->instruments[pos] == instrument)
        {
            cbox_rt_array_remove(scene->rt, (void ***)&scene->instruments, &scene->instrument_count, pos);            
            g_hash_table_remove(scene->instrument_hash, instrument->module->instance_name);
            instrument->scene = NULL;
            return TRUE;
        }
    }
    return FALSE;
}

gboolean cbox_scene_insert_aux_bus(struct cbox_scene *scene, struct cbox_aux_bus *aux_bus)
{
    struct cbox_aux_bus **aux_buses = malloc(sizeof(struct cbox_aux_bus *) * (scene->aux_bus_count + 1));
    memcpy(aux_buses, scene->aux_buses, sizeof(struct cbox_aux_bus *) * (scene->aux_bus_count));
    aux_buses[scene->aux_bus_count] = aux_bus;
    free(cbox_rt_swap_pointers_and_update_count(scene->rt, (void **)&scene->aux_buses, aux_buses, &scene->aux_bus_count, scene->aux_bus_count + 1));
    return TRUE;
}

void cbox_scene_remove_aux_bus(struct cbox_scene *scene, struct cbox_aux_bus *removed)
{
    int pos = -1;
    for (int i = 0; i < scene->aux_bus_count; i++)
    {
        if (scene->aux_buses[i] == removed)
        {
            pos = i;
            break;
        }
    }
    assert(pos != -1);
    for (int i = 0; i < scene->instrument_count; i++)
        cbox_instrument_disconnect_aux_bus(scene->instruments[i], removed);
    
    struct cbox_aux_bus **aux_buses = malloc(sizeof(struct cbox_aux_bus *) * (scene->aux_bus_count - 1));
    memcpy(aux_buses, scene->aux_buses, sizeof(struct cbox_aux_bus *) * pos);
    memcpy(aux_buses + pos, scene->aux_buses + pos + 1, sizeof(struct cbox_aux_bus *) * (scene->aux_bus_count - pos - 1));
    free(cbox_rt_swap_pointers_and_update_count(scene->rt, (void **)&scene->aux_buses, aux_buses, &scene->aux_bus_count, scene->aux_bus_count - 1));
}

struct cbox_aux_bus *cbox_scene_get_aux_bus(struct cbox_scene *scene, const char *name, int allow_load, GError **error)
{
    for (int i = 0; i < scene->aux_bus_count; i++)
    {
        if (!strcmp(scene->aux_buses[i]->name, name))
        {
            return scene->aux_buses[i];
        }
    }
    if (!allow_load)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Aux bus not found: %s", name);
        return FALSE;
    }
    struct cbox_aux_bus *bus = cbox_aux_bus_load(scene, name, scene->rt, error);
    if (!bus)
        return NULL;
    return bus;
}

static int write_events_to_instrument_ports(struct cbox_scene *scene, struct cbox_midi_buffer *source)
{
    uint32_t i;

    for (i = 0; i < scene->instrument_count; i++)
        cbox_midi_buffer_clear(&scene->instruments[i]->module->midi_input);

    if (!source)
        return 0;

    uint32_t event_count = cbox_midi_buffer_get_count(source);
    for (i = 0; i < event_count; i++)
    {
        const struct cbox_midi_event *event = cbox_midi_buffer_get_event(source, i);
        
        // XXXKF ignore sysex for now
        if (event->size >= 4)
            continue;
        
        for (int l = 0; l < scene->layer_count; l++)
        {
            struct cbox_layer *lp = scene->layers[l];
            if (!lp->enabled)
                continue;
            uint8_t data[4] = {0, 0, 0, 0};
            memcpy(data, event->data_inline, event->size);
            if (data[0] < 0xF0) // per-channel messages
            {
                int cmd = data[0] >> 4;
                // filter on MIDI channel
                if (lp->in_channel >= 0 && lp->in_channel != (data[0] & 0x0F))
                    continue;
                // force output channel
                if (lp->out_channel >= 0)
                    data[0] = (data[0] & 0xF0) + (lp->out_channel & 0x0F);
                if (cmd >= 8 && cmd <= 10)
                {
                    if (cmd == 10 && lp->disable_aftertouch)
                        continue;
                    // note filter
                    if (data[1] < lp->low_note || data[1] > lp->high_note)
                        continue;
                    // transpose
                    int transpose = lp->transpose + (lp->ignore_scene_transpose ? 0 : scene->transpose);
                    if (transpose)
                    {
                        int note = data[1] + transpose;
                        if (note < 0 || note > 127)
                            continue;
                        data[1] = (uint8_t)note;
                    }
                    // fixed note
                    if (lp->fixed_note != -1)
                    {
                        data[1] = (uint8_t)lp->fixed_note;
                    }
                }
                else if (cmd == 11 && data[1] == 64 && lp->invert_sustain)
                {
                    data[2] = 127 - data[2];
                }
                else if (lp->ignore_program_changes && cmd == 11 && (data[1] == 0 || data[1] == 32))
                    continue;
                else if (cmd == 13 && lp->disable_aftertouch)
                    continue;
                else if (cmd == 12 && lp->ignore_program_changes)
                    continue;
            }
            if (!cbox_midi_buffer_write_event(&lp->instrument->module->midi_input, event->time, data, event->size))
                return -i;
            if (lp->consume)
                break;
        }
    }
    
    return event_count;
}

void cbox_scene_render(struct cbox_scene *scene, uint32_t nframes, float *output_buffers[])
{
    int n, i, j;

    if (scene->rt && scene->rt->io)
    {
        struct cbox_io *io = scene->rt->io;
        for (i = 0; i < io->io_env.input_count; i++)
        {
            if (IS_RECORDING_SOURCE_CONNECTED(scene->rec_mono_inputs[i]))
                cbox_recording_source_push(&scene->rec_mono_inputs[i], (const float **)&io->input_buffers[i], nframes);
        }
        for (i = 0; i < io->io_env.input_count / 2; i++)
        {
            if (IS_RECORDING_SOURCE_CONNECTED(scene->rec_stereo_inputs[i]))
            {
                const float *buf[2] = { io->input_buffers[i * 2], io->input_buffers[i * 2 + 1] };
                cbox_recording_source_push(&scene->rec_stereo_inputs[i], buf, nframes);
            }
        }
    }
    
    for(struct cbox_adhoc_pattern **ppat = &scene->adhoc_patterns; *ppat; )
    {
        cbox_midi_buffer_clear(&(*ppat)->output_buffer);
        if ((*ppat)->completed)
        {
            struct cbox_adhoc_pattern *retired = *ppat;
            *ppat = retired->next;
            retired->next = scene->retired_adhoc_patterns;
            scene->retired_adhoc_patterns = retired;
        }
        else
        {
            cbox_adhoc_pattern_render((*ppat), 0, nframes);
            ppat = &((*ppat)->next);
        }
    }
    
    // XXXKF implement full cleanup, not only the front of the queue
    if(scene->adhoc_patterns && scene->adhoc_patterns->completed)
    {
        struct cbox_adhoc_pattern *top = scene->adhoc_patterns;
        cbox_midi_buffer_clear(&top->output_buffer);
        scene->adhoc_patterns = top->next;
    }


    cbox_midi_buffer_clear(&scene->midibuf_total);
    cbox_midi_merger_render(&scene->scene_input_merger);

    write_events_to_instrument_ports(scene, &scene->midibuf_total);

    for (n = 0; n < scene->aux_bus_count; n++)
    {
        for (i = 0; i < nframes; i ++)
        {
            scene->aux_buses[n]->input_bufs[0][i] = 0.f;
            scene->aux_buses[n]->input_bufs[1][i] = 0.f;
        }
    }
    
    for (n = 0; n < scene->instrument_count; n++)
    {
        struct cbox_instrument *instr = scene->instruments[n];
        struct cbox_module *module = instr->module;
        int event_count = instr->module->midi_input.count;
        int cur_event = 0;
        uint32_t highwatermark = 0;
        cbox_sample_t channels[CBOX_MAX_AUDIO_PORTS][CBOX_BLOCK_SIZE];
        cbox_sample_t *outputs[CBOX_MAX_AUDIO_PORTS];
        for (i = 0; i < module->outputs; i++)
            outputs[i] = channels[i];
        
        for (i = 0; i < nframes; i += CBOX_BLOCK_SIZE)
        {            
            if (i >= highwatermark)
            {
                while(cur_event < event_count)
                {
                    const struct cbox_midi_event *event = cbox_midi_buffer_get_event(&module->midi_input, cur_event);
                    if (event)
                    {
                        if (event->time <= i)
                            (*module->process_event)(module, cbox_midi_event_get_data(event), event->size);
                        else
                        {
                            highwatermark = event->time;
                            break;
                        }
                    }
                    else
                        break;
                    
                    cur_event++;
                }
            }
            (*module->process_block)(module, NULL, outputs);
            for (int o = 0; o < module->outputs / 2; o++)
            {
                struct cbox_instrument_output *oobj = &instr->outputs[o];
                struct cbox_module *insert = oobj->insert;
                float gain = oobj->gain;
                if (IS_RECORDING_SOURCE_CONNECTED(oobj->rec_dry))
                    cbox_recording_source_push(&oobj->rec_dry, (const float **)(outputs + 2 * o), CBOX_BLOCK_SIZE);
                if (insert && !insert->bypass)
                    (*insert->process_block)(insert, outputs + 2 * o, outputs + 2 * o);
                if (IS_RECORDING_SOURCE_CONNECTED(oobj->rec_wet))
                    cbox_recording_source_push(&oobj->rec_wet, (const float **)(outputs + 2 * o), CBOX_BLOCK_SIZE);
                float *leftbuf, *rightbuf;
                if (o < module->aux_offset / 2)
                {
                    int leftch = oobj->output_bus * 2;
                    int rightch = leftch + 1;
                    leftbuf = output_buffers[leftch];
                    rightbuf = output_buffers[rightch];
                }
                else
                {
                    int bus = o - module->aux_offset / 2;
                    struct cbox_aux_bus *busobj = instr->aux_outputs[bus];
                    if (busobj == NULL)
                        continue;
                    leftbuf = busobj->input_bufs[0];
                    rightbuf = busobj->input_bufs[1];
                }
                if (leftbuf && rightbuf)
                {
                    for (j = 0; j < CBOX_BLOCK_SIZE; j++)
                    {
                        leftbuf[i + j] += gain * channels[2 * o][j];
                        rightbuf[i + j] += gain * channels[2 * o + 1][j];
                    }
                }
                else
                {
                    for (j = 0; j < CBOX_BLOCK_SIZE; j++)
                    {
                        if (leftbuf)
                            leftbuf[i + j] += gain * channels[2 * o][j];
                        if (rightbuf)
                            rightbuf[i + j] += gain * channels[2 * o + 1][j];
                    }
                }
            }
        }
        while(cur_event < event_count)
        {
            const struct cbox_midi_event *event = cbox_midi_buffer_get_event(&module->midi_input, cur_event);
            if (event)
            {
                (*module->process_event)(module, cbox_midi_event_get_data(event), event->size);
            }
            else
                break;
            
            cur_event++;
        }
    }
    
    for (n = 0; n < scene->aux_bus_count; n++)
    {
        struct cbox_aux_bus *bus = scene->aux_buses[n];
        float left[CBOX_BLOCK_SIZE], right[CBOX_BLOCK_SIZE];
        float *outputs[2] = {left, right};
        for (i = 0; i < nframes; i += CBOX_BLOCK_SIZE)
        {
            float *inputs[2];
            inputs[0] = &bus->input_bufs[0][i];
            inputs[1] = &bus->input_bufs[1][i];
            bus->module->process_block(bus->module, inputs, outputs);
            for (int j = 0; j < CBOX_BLOCK_SIZE; j++)
            {
                output_buffers[0][i + j] += left[j];
                output_buffers[1][i + j] += right[j];
            }
        }
    }

    int output_count = scene->engine->io_env.output_count;
    // XXXKF this assumes that the buffers are zeroed on start - which isn't true if there are multiple scenes
    for (i = 0; i < output_count; i++)
    {
        if (IS_RECORDING_SOURCE_CONNECTED(scene->rec_mono_outputs[i]))
            cbox_recording_source_push(&scene->rec_mono_outputs[i], (const float **)&output_buffers[i], nframes);
    }
    for (i = 0; i < output_count / 2; i++)
    {
        if (IS_RECORDING_SOURCE_CONNECTED(scene->rec_stereo_outputs[i]))
        {
            const float *buf[2] = { output_buffers[i * 2], output_buffers[i * 2 + 1] };
            cbox_recording_source_push(&scene->rec_stereo_outputs[i], buf, nframes);
        }
    }
}

void cbox_scene_clear(struct cbox_scene *scene)
{
    g_free(scene->name);
    g_free(scene->title);
    scene->name = g_strdup("");
    scene->title = g_strdup("");
    while(scene->layer_count > 0)
    {
        struct cbox_layer *layer = cbox_scene_remove_layer(scene, 0);
        CBOX_DELETE(layer);
    }
            
    while(scene->aux_bus_count > 0)
        CBOX_DELETE(scene->aux_buses[scene->aux_bus_count - 1]);
}

static struct cbox_instrument *create_instrument(struct cbox_scene *scene, struct cbox_module *module)
{
    int auxes = (module->outputs - module->aux_offset) / 2;

    struct cbox_instrument *instr = malloc(sizeof(struct cbox_instrument));
    CBOX_OBJECT_HEADER_INIT(instr, cbox_instrument, CBOX_GET_DOCUMENT(scene));
    instr->scene = scene;
    instr->module = module;
    instr->outputs = calloc(module->outputs / 2, sizeof(struct cbox_instrument_output));
    instr->refcount = 0;
    instr->aux_outputs = calloc(auxes, sizeof(struct cbox_aux_bus *));
    instr->aux_output_names = calloc(auxes, sizeof(char *));
    instr->aux_output_count = auxes;

    for (int i = 0; i < module->outputs / 2; i ++)
        cbox_instrument_output_init(&instr->outputs[i], scene, module->engine->io_env.buffer_size);
    
    return instr;
}

struct cbox_instrument *cbox_scene_create_instrument(struct cbox_scene *scene, const char *instrument_name, const char *engine_name, GError **error)
{
    gpointer value = g_hash_table_lookup(scene->instrument_hash, instrument_name);
    if (value)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Instrument already exists: '%s'", instrument_name);
        return NULL;
    }

    struct cbox_document *doc = CBOX_GET_DOCUMENT(scene);
    struct cbox_module_manifest *mptr = NULL;
    struct cbox_instrument *instr = NULL;
    struct cbox_module *module = NULL;
        
    mptr = cbox_module_manifest_get_by_name(engine_name);
    if (!mptr)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "No engine called '%s'", engine_name);
        return NULL;
    }

    module = cbox_module_manifest_create_module(mptr, NULL, doc, scene->rt, scene->engine, instrument_name, error);
    if (!module)
    {
        cbox_force_error(error);
        g_prefix_error(error, "Cannot create engine '%s' for instrument '%s': ", engine_name, instrument_name);
        return NULL;
    }
    
    instr = create_instrument(scene, module);
    
    cbox_command_target_init(&instr->cmd_target, cbox_instrument_process_cmd, instr);
    g_hash_table_insert(scene->instrument_hash, g_strdup(instrument_name), instr);
    CBOX_OBJECT_REGISTER(instr);
    
    return instr;
}

struct cbox_instrument *cbox_scene_get_instrument_by_name(struct cbox_scene *scene, const char *name, gboolean load, GError **error)
{
    struct cbox_module_manifest *mptr = NULL;
    struct cbox_instrument *instr = NULL;
    struct cbox_module *module = NULL;
    gchar *instr_section = NULL;
    gpointer value = g_hash_table_lookup(scene->instrument_hash, name);
    const char *cv, *instr_engine;
    struct cbox_document *doc = CBOX_GET_DOCUMENT(scene);
    assert(scene);
    
    if (value)
        return value;
    if (!load)
        return NULL;
    
    instr_section = g_strdup_printf("instrument:%s", name);
    
    if (!cbox_config_has_section(instr_section))
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "No config section for instrument '%s'", name);
        goto error;
    }
    
    instr_engine = cbox_config_get_string(instr_section, "engine");
    if (!instr_engine)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "Engine not specified in instrument '%s'", name);
        goto error;
    }

    mptr = cbox_module_manifest_get_by_name(instr_engine);
    if (!mptr)
    {
        g_set_error(error, CBOX_MODULE_ERROR, CBOX_MODULE_ERROR_FAILED, "No engine called '%s'", instr_engine);
        goto error;
    }
    
    // cbox_module_manifest_dump(mptr);
    
    module = cbox_module_manifest_create_module(mptr, instr_section, doc, scene->rt, scene->engine, name, error);
    if (!module)
    {
        cbox_force_error(error);
        g_prefix_error(error, "Cannot create engine '%s' for instrument '%s': ", instr_engine, name);
        goto error;
    }
    
    instr = create_instrument(scene, module);

    for (int i = 0; i < module->outputs / 2; i ++)
    {
        struct cbox_instrument_output *oobj = instr->outputs + i;
        
        gchar *key = i == 0 ? g_strdup("output_bus") : g_strdup_printf("output%d_bus", 1 + i);
        oobj->output_bus = cbox_config_get_int(instr_section, key, 1) - 1;
        g_free(key);
        key = i == 0 ? g_strdup("gain") : g_strdup_printf("gain%d", 1 + i);
        oobj->gain = cbox_config_get_gain_db(instr_section, key, 0);
        g_free(key);
        
        key = i == 0 ? g_strdup("insert") : g_strdup_printf("insert%d", 1 + i);
        cv = cbox_config_get_string(instr_section, key);
        g_free(key);
        
        if (cv)
        {
            oobj->insert = cbox_module_new_from_fx_preset(cv, CBOX_GET_DOCUMENT(scene), module->rt, scene->engine, error);
            if (!oobj->insert)
            {
                cbox_force_error(error);
                g_prefix_error(error, "Cannot instantiate effect preset '%s' for instrument '%s': ", cv, name);
            }
        }
    }

    for (int i = 0; i < instr->aux_output_count; i++)
    {
        instr->aux_outputs[i] = NULL;
        
        gchar *key = g_strdup_printf("aux%d", 1 + i);
        gchar *value = cbox_config_get_string(instr_section, key);
        instr->aux_output_names[i] = value ? g_strdup(value) : NULL;
        g_free(key);
        
    }
    cbox_command_target_init(&instr->cmd_target, cbox_instrument_process_cmd, instr);
    
    free(instr_section);
    
    g_hash_table_insert(scene->instrument_hash, g_strdup(name), instr);
    CBOX_OBJECT_REGISTER(instr);
    
    // cbox_recording_source_attach(&instr->outputs[0].rec_dry, cbox_recorder_new_stream("output.wav"));
    
    return instr;
    
error:
    free(instr_section);
    return NULL;
}

static struct cbox_recording_source *create_rec_sources(struct cbox_scene *scene, int buffer_size, int count, int channels)
{
    struct cbox_recording_source *s = malloc(sizeof(struct cbox_recording_source) * count);
    for (int i = 0; i < count; i++)
        cbox_recording_source_init(&s[i], scene, buffer_size, channels);
    return s;
}

static void destroy_rec_sources(struct cbox_recording_source *s, int count)
{
    for (int i = 0; i < count; i++)
        cbox_recording_source_uninit(&s[i]);
    free(s);
}

struct cbox_scene *cbox_scene_new(struct cbox_document *document, struct cbox_engine *engine)
{
    struct cbox_scene *s = malloc(sizeof(struct cbox_scene));
    if (!s)
        return NULL;

    CBOX_OBJECT_HEADER_INIT(s, cbox_scene, document);
    s->engine = engine;
    s->rt = engine ? engine->rt : NULL;
    s->instrument_hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    s->name = g_strdup("");
    s->title = g_strdup("");
    s->layers = NULL;
    s->aux_buses = NULL;
    s->instruments = NULL;
    s->layer_count = 0;
    s->instrument_count = 0;
    s->aux_bus_count = 0;
    cbox_command_target_init(&s->cmd_target, cbox_scene_process_cmd, s);
    s->transpose = 0;
    s->connected_inputs = NULL;
    s->connected_input_count = 0;
    s->enable_default_song_input = TRUE;
    s->enable_default_external_input = TRUE;

    cbox_midi_buffer_init(&s->midibuf_total);
    cbox_midi_merger_init(&s->scene_input_merger, &s->midibuf_total);

    int buffer_size = engine->io_env.buffer_size;
    s->rec_mono_inputs = create_rec_sources(s, buffer_size, engine->io_env.input_count, 1);
    s->rec_stereo_inputs = create_rec_sources(s, buffer_size, engine->io_env.input_count / 2, 2);
    s->rec_mono_outputs = create_rec_sources(s, buffer_size, engine->io_env.output_count, 1);
    s->rec_stereo_outputs = create_rec_sources(s, buffer_size, engine->io_env.output_count / 2, 2);
    s->adhoc_patterns = NULL;
    s->retired_adhoc_patterns = NULL;
    
    CBOX_OBJECT_REGISTER(s);
    
    cbox_engine_add_scene(s->engine, s);
    cbox_scene_update_connected_inputs(s);
    return s;
}

void cbox_scene_update_connected_inputs(struct cbox_scene *scene)
{
    if (!scene->rt || !scene->rt->io)
        return;
    
    // This is called when a MIDI Input port has been created, connected/disconnected
    // or is about to be removed (and then the removing flag will be set)
    for (int i = 0; i < scene->connected_input_count; )
    {
        struct cbox_midi_input *input = scene->connected_inputs[i];
        if (input->removing || !cbox_uuid_equal(&input->output, &scene->_obj_hdr.instance_uuid))
        {
            cbox_midi_merger_disconnect(&scene->scene_input_merger, &input->buffer, scene->rt);
            cbox_rt_array_remove(scene->rt, (void ***)&scene->connected_inputs, &scene->connected_input_count, i);
        }
        else
            i++;
    }
    for (GSList *p = scene->rt->io->midi_inputs; p; p = p->next)
    {
        struct cbox_midi_input *input = p->data;
        if (cbox_uuid_equal(&input->output, &scene->_obj_hdr.instance_uuid))
        {
            gboolean found = FALSE;
            for (int i = 0; i < scene->connected_input_count; i++)
            {
                if (scene->connected_inputs[i] == input)
                {
                    found = TRUE;
                    break;
                }
            }
            if (!found)
            {
                cbox_midi_merger_connect(&scene->scene_input_merger, &input->buffer, scene->rt);
                cbox_rt_array_insert(scene->rt, (void ***)&scene->connected_inputs, &scene->connected_input_count, -1, input);
            }
        }
    }
    if (scene->enable_default_song_input)
    {
        cbox_midi_merger_connect(&scene->scene_input_merger, &scene->engine->midibuf_aux, scene->rt);
        cbox_midi_merger_connect(&scene->scene_input_merger, &scene->engine->midibuf_song, scene->rt);
    }
    else
    {
        cbox_midi_merger_disconnect(&scene->scene_input_merger, &scene->engine->midibuf_aux, scene->rt);
        cbox_midi_merger_disconnect(&scene->scene_input_merger, &scene->engine->midibuf_song, scene->rt);
    }
    
    if (scene->enable_default_external_input)
        cbox_midi_merger_connect(&scene->scene_input_merger, &scene->engine->midibuf_jack, scene->rt);
    else
        cbox_midi_merger_disconnect(&scene->scene_input_merger, &scene->engine->midibuf_jack, scene->rt);
    
}

static void free_adhoc_pattern_list(struct cbox_scene *scene, struct cbox_adhoc_pattern *ap)
{
    while(ap)
    {
        struct cbox_adhoc_pattern *tmp = ap;
        ap = ap->next;
        tmp->next = NULL;
        cbox_midi_merger_disconnect(&scene->scene_input_merger, &ap->output_buffer, scene->rt);
        cbox_adhoc_pattern_destroy(tmp);
    }
}

struct play_adhoc_pattern_arg
{
    struct cbox_scene *scene;
    struct cbox_adhoc_pattern *ap;
    struct cbox_adhoc_pattern *retired;
};

static int play_adhoc_pattern_execute(void *arg_)
{
    struct play_adhoc_pattern_arg *arg = arg_;
    
    if (arg->ap)
    {
        struct cbox_adhoc_pattern *ap = arg->scene->adhoc_patterns;
        
        // If there is already an adhoc pattern with a given non-zero id, stop it
        // and release all the pending notes. Retry until all the notes are
        // released.
        if (arg->ap->id)
        {
            while(ap && ap->id != arg->ap->id)
                ap = ap->next;
            if (ap)
            {
                ap->completed = TRUE;
                if (ap->active_notes.channels_active)
                    return 0;
            }
        }
        
        arg->ap->next = arg->scene->adhoc_patterns;
        arg->scene->adhoc_patterns = arg->ap;
    }
    arg->retired = arg->scene->retired_adhoc_patterns;
    arg->scene->retired_adhoc_patterns = NULL;
    // XXXKF should convert pattern length into sample position instead of assuming 0x7FFFFFFF (though it likely doesn't matter)
    cbox_midi_clip_playback_set_pattern(&arg->ap->playback, arg->ap->pattern_playback, 0, 0x7FFFFFFF, 0, 0);
    return 1;
}

void cbox_scene_play_adhoc_pattern(struct cbox_scene *scene, struct cbox_adhoc_pattern *ap)
{
    static struct cbox_rt_cmd_definition cmd = { NULL, play_adhoc_pattern_execute, NULL };
    struct play_adhoc_pattern_arg arg = { scene, ap, NULL };
    cbox_midi_merger_connect(&scene->scene_input_merger, &ap->output_buffer, scene->rt);
    cbox_rt_execute_cmd_sync(scene->rt, &cmd, &arg);
    if (arg.retired)
        free_adhoc_pattern_list(scene, arg.retired);
}

static void cbox_scene_destroyfunc(struct cbox_objhdr *objhdr)
{
    struct cbox_scene *scene = CBOX_H2O(objhdr);
    cbox_midi_merger_disconnect(&scene->scene_input_merger, &scene->engine->midibuf_aux,  scene->rt);
    cbox_midi_merger_disconnect(&scene->scene_input_merger, &scene->engine->midibuf_jack, scene->rt);
    cbox_midi_merger_disconnect(&scene->scene_input_merger, &scene->engine->midibuf_song, scene->rt);
    cbox_engine_remove_scene(scene->engine, scene);
    cbox_scene_clear(scene);
    g_free(scene->name);
    g_free(scene->title);
    assert(scene->instrument_count == 0);
    free(scene->layers);
    free(scene->aux_buses);
    free(scene->instruments);
    g_hash_table_destroy(scene->instrument_hash);
    free(scene->connected_inputs);

    destroy_rec_sources(scene->rec_mono_inputs, scene->engine->io_env.input_count);
    destroy_rec_sources(scene->rec_stereo_inputs, scene->engine->io_env.input_count / 2);
    destroy_rec_sources(scene->rec_mono_outputs, scene->engine->io_env.output_count);
    destroy_rec_sources(scene->rec_stereo_outputs, scene->engine->io_env.output_count / 2);
    
    free_adhoc_pattern_list(scene, scene->retired_adhoc_patterns);
    free_adhoc_pattern_list(scene, scene->adhoc_patterns);
    cbox_midi_merger_close(&scene->scene_input_merger);    
    free(scene);
}
