#include "app.h"
#include "blob.h"
#include "instr.h"
#include "rt.h"
#include "sampler.h"
#include "sampler_prg.h"

/////////////////////////////////////////////////////////////////////////////////

struct sampler_rll *sampler_rll_new_from_program(struct sampler_program *prg)
{
    struct sampler_rll *rll = malloc(sizeof(struct sampler_rll));
    rll->layers = NULL;
    rll->layers_release = NULL;
    rll->layers_oncc = NULL;
    for (int i = 0; i < 4; i++)
        rll->cc_trigger_bitmask[i] = 0;

    for (GSList *p = prg->all_layers; p; p = g_slist_next(p))
    {
        struct sampler_layer *l = p->data;
        if (l->data.on_cc.has_locc || l->data.on_cc.has_hicc)
        {
            int cc = l->data.on_cc.cc_number;
            rll->layers_oncc = g_slist_prepend(rll->layers_oncc, l);
            rll->cc_trigger_bitmask[cc >> 5] |= 1 << (cc & 31);
        }
        else if (l->data.trigger == stm_release)
            rll->layers_release = g_slist_prepend(rll->layers_release, l);
        else
            rll->layers = g_slist_prepend(rll->layers, l);
    }
    return rll;
}

void sampler_rll_destroy(struct sampler_rll *rll)
{
    g_slist_free(rll->layers);
    g_slist_free(rll->layers_release);
    g_slist_free(rll->layers_oncc);
    free(rll);
}
