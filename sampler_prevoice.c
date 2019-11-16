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
    pv->age += CBOX_BLOCK_SIZE;
    if (pv->age >= (layer_data->delay + pv->delay_computed) * m->module.srate)
        return 1;
    
    return 0;
}

