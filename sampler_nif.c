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
    pv->delay_computed += nif->param * pv->channel->cc[nif->variant] * (1.0 / 127.0);
}

void sampler_nif_addrandomdelay(struct sampler_noteinitfunc *nif, struct sampler_prevoice *pv)
{
    pv->delay_computed += nif->param * rand() * (1.0 / RAND_MAX);
}

void sampler_nif_vel2pitch(struct sampler_noteinitfunc *nif, struct sampler_voice *v)
{
    v->pitch_shift += nif->param * v->vel * (1.0 / 127.0);
}

void sampler_nif_vel2reloffset(struct sampler_noteinitfunc *nif, struct sampler_voice *v)
{
    v->reloffset += nif->param * v->vel * (1.0 / 127.0);
}

void sampler_nif_cc2reloffset(struct sampler_noteinitfunc *nif, struct sampler_voice *v)
{
    v->reloffset += nif->param * v->channel->cc[nif->variant] * (1.0 / 127.0);
}

void sampler_nif_addrandom(struct sampler_noteinitfunc *nif, struct sampler_voice *v)
{
    float rnd = rand() * 1.0 / RAND_MAX;
    switch(nif->variant)
    {
        case 0:
            v->gain_shift += rnd * nif->param;
            break;
        case 1:
            v->cutoff_shift += rnd * nif->param;
            break;
        case 2:
            v->pitch_shift += rnd * nif->param; // this is in cents
            break;
    }
}

void sampler_nif_vel2env(struct sampler_noteinitfunc *nif, struct sampler_voice *v)
{
    int env_type = (nif->variant) >> 4;
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
    if (env->shape != &v->dyn_envs[env_type])
    {
        memcpy(&v->dyn_envs[env_type], env->shape, sizeof(struct cbox_envelope_shape));
        env->shape = &v->dyn_envs[env_type];
    }
    float param = nif->param * v->vel * (1.0 / 127.0);
    if ((nif->variant & 15) == snif_env_sustain || (nif->variant & 15) == snif_env_start)
        param *= 0.01;
    cbox_envelope_modify_dahdsr(env->shape, nif->variant & 15, param, v->channel->module->module.srate * (1.0 / CBOX_BLOCK_SIZE));
}

