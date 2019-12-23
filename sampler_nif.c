#include "config-api.h"
#include "dspmath.h"
#include "errors.h"
#include "midi.h"
#include "module.h"
#include "rt.h"
#include "sampler.h"
#include "sampler_impl.h"

//////////////////////////////////////////////////////////////////////////
// Note initialisation functions

void sampler_nif_cc2delay(struct sampler_noteinitfunc *nif, struct sampler_prevoice *pv)
{
    pv->delay_computed += nif->value.value * sampler_channel_getcc_prevoice(pv->channel, pv, nif->key.variant);
}

void sampler_nif_addrandomdelay(struct sampler_noteinitfunc *nif, struct sampler_prevoice *pv)
{
    pv->delay_computed += nif->value.value * rand() * (1.0 / RAND_MAX);
}

void sampler_nif_syncbeats(struct sampler_noteinitfunc *nif, struct sampler_prevoice *pv)
{
    if (nif->value.value > 0)
    {
        pv->sync_beats = nif->value.value;
        double cur_beat = sampler_get_current_beat(pv->channel->module);
        pv->sync_initial_time = cur_beat;
        double cur_rel_beat = fmod(cur_beat, pv->sync_beats);
        double bar_start = cur_beat - cur_rel_beat;
        if (pv->layer_data->sync_offset <= cur_rel_beat) // trigger in next bar
            pv->sync_trigger_time = bar_start + pv->sync_beats + pv->layer_data->sync_offset;
        else // trigger in the same bar
            pv->sync_trigger_time = bar_start + pv->layer_data->sync_offset;
        // printf("cur_beat %f trigger %f offset %f\n", cur_beat, pv->sync_trigger_time, pv->layer_data->sync_offset);
    }
}

void sampler_nif_vel2pitch(struct sampler_noteinitfunc *nif, struct sampler_voice *v)
{
    v->pitch_shift += nif->value.value * v->vel * (1.0 / 127.0);
}

void sampler_nif_vel2offset(struct sampler_noteinitfunc *nif, struct sampler_voice *v)
{
    v->offset += nif->value.value * v->vel * (1.0 / 127.0);
}

void sampler_nif_cc2offset(struct sampler_noteinitfunc *nif, struct sampler_voice *v)
{
    v->offset += nif->value.value * sampler_channel_getcc(v->channel, v, nif->key.variant);
}

void sampler_nif_vel2reloffset(struct sampler_noteinitfunc *nif, struct sampler_voice *v)
{
    v->reloffset += nif->value.value * v->vel * (1.0 / 127.0);
}

void sampler_nif_cc2reloffset(struct sampler_noteinitfunc *nif, struct sampler_voice *v)
{
    v->reloffset += nif->value.value * sampler_channel_getcc(v->channel, v, nif->key.variant);
}

void sampler_nif_addrandom(struct sampler_noteinitfunc *nif, struct sampler_voice *v)
{
    float rnd = rand() * 1.0 / RAND_MAX;
    switch(nif->key.variant)
    {
        case 0:
            v->gain_shift += rnd * nif->value.value;
            break;
        case 1:
            v->cutoff_shift += rnd * nif->value.value;
            break;
        case 2:
            v->pitch_shift += rnd * nif->value.value; // this is in cents
            break;
    }
}

static void modify_env_stage_by_nif(struct sampler_noteinitfunc *nif, struct sampler_voice *v, uint32_t variant, float value)
{
    int env_type = variant >> 4;
    struct cbox_envelope *env = NULL;
    switch(env_type)
    {
        case 0:
            env = &v->amp_env;
            break;
        case 1:
            env = &v->filter_env;
            break;
        case 2:
            env = &v->pitch_env;
            break;
        default:
            assert(0);
    }
    if (env->shape != &v->vel_envs[env_type])
    {
        memcpy(&v->vel_envs[env_type], env->shape, sizeof(struct cbox_envelope_shape));
        env->shape = &v->vel_envs[env_type];
    }
    float param = nif->value.value * value;
    if ((variant & 15) == snif_env_sustain || (variant & 15) == snif_env_start)
        param *= 0.01;
    cbox_envelope_modify_dahdsr(env->shape, variant & 15, param, v->channel->module->module.srate * (1.0 / CBOX_BLOCK_SIZE));
}

void sampler_nif_vel2env(struct sampler_noteinitfunc *nif, struct sampler_voice *v)
{
    modify_env_stage_by_nif(nif, v, nif->key.variant, v->vel * (1.0 / 127.0));
}
