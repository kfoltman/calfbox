#include "config.h"
#include "config-api.h"
#include "dspmath.h"
#include "errors.h"
#include "midi.h"
#include "module.h"
#include "rt.h"
#include "sampler.h"
#include "sampler_impl.h"

void sampler_prevoice_start(struct sampler_prevoice *pv, struct sampler_channel *channel, struct sampler_layer_data *l, int note, int vel)
{
    pv->channel = channel;
    pv->layer_data = l;
    pv->note = note;
    pv->vel = vel;
    pv->age = 0;
    pv->delay_computed = 0.f;
    pv->sync_beats = -1;
    pv->sync_initial_time = -1;
    pv->sync_trigger_time = -1;

    GSList *nif = pv->layer_data->prevoice_nifs;
    while(nif)
    {
        struct sampler_noteinitfunc *p = nif->data;
        p->notefunc_prevoice(p, pv);
        nif = nif->next;
    }
    sampler_prevoice_unlink(&channel->module->prevoices_free, pv);
    sampler_prevoice_link(&channel->module->prevoices_running, pv);
}

void sampler_prevoice_link(struct sampler_prevoice **pv, struct sampler_prevoice *v)
{
    v->prev = NULL;
    v->next = *pv;
    if (*pv)
        (*pv)->prev = v;
    *pv = v;
}

void sampler_prevoice_unlink(struct sampler_prevoice **pv, struct sampler_prevoice *v)
{
    if (*pv == v)
        *pv = v->next;
    if (v->prev)
        v->prev->next = v->next;
    if (v->next)
        v->next->prev = v->prev;
    v->prev = NULL;
    v->next = NULL;
}

int sampler_prevoice_process(struct sampler_prevoice *pv, struct sampler_module *m)
{
    struct sampler_layer_data *layer_data = pv->layer_data;
    if (pv->sync_beats != -1)
    {
        double cur_beat = sampler_get_current_beat(m);

        if (cur_beat < pv->sync_initial_time - 0.001 || cur_beat >= pv->sync_trigger_time + 1)
        {
            gboolean backward_jump = cur_beat < pv->sync_initial_time;
            // printf("Recalc: time %f, initial %f, delta %f, trigger %f\n", cur_beat, pv->sync_initial_time, cur_beat - pv->sync_initial_time, pv->sync_trigger_time);
            // Recalculate after seek/looping etc
            pv->sync_initial_time = cur_beat;
            double cur_rel_beat = fmod(cur_beat, pv->sync_beats);
            double bar_start = cur_beat - cur_rel_beat;
            if (pv->layer_data->sync_offset <= cur_rel_beat && !backward_jump) // trigger in next bar
                pv->sync_trigger_time = bar_start + pv->sync_beats + pv->layer_data->sync_offset;
            else // trigger in the same bar
                pv->sync_trigger_time = bar_start + pv->layer_data->sync_offset;
        }
        if (cur_beat < pv->sync_trigger_time)
            return 0;
        // Let the other logic (note delay etc.) take over
        pv->sync_beats = -1;
    }
    pv->age += CBOX_BLOCK_SIZE;
    if (pv->age >= (layer_data->delay + pv->delay_computed) * m->module.srate)
        return 1;
    
    return 0;
}

