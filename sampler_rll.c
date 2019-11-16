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

    uint16_t lo_count[129], hi_count[128], low = 127, high = 0;
    for (int i = 0; i < 128; i++)
        lo_count[i] = hi_count[i] = 0;

    // XXXKF handle 'key' field without relying on the existing ugly hack
    for (GSList *p = prg->all_layers; p; p = g_slist_next(p))
    {
        struct sampler_layer *l = p->data;
        if (!l->data.on_cc.is_active &&
            l->data.lokey >= 0 && l->data.lokey <= 127 &&
            l->data.hikey >= 0 && l->data.hikey <= 127)
        {
            lo_count[l->data.lokey]++;
            hi_count[l->data.hikey]++;
            if (l->data.lokey < low)
                low = l->data.lokey;
            if (l->data.hikey > high)
                high = l->data.hikey;
        }
    }
    rll->lokey = low;
    rll->hikey = high;
    uint32_t range_count = 1;
    for (int i = low + 1; i <= high; ++i)
    {
        rll->ranges_by_key[i - 1] = range_count - 1;
        if (hi_count[i - 1] || lo_count[i])
            range_count++;
    }
    rll->ranges_by_key[high] = range_count - 1;
    rll->layers_by_range = calloc(range_count, sizeof(GSList *));
    rll->release_layers_by_range = calloc(range_count, sizeof(GSList *));
    rll->layers_by_range_count = range_count;
    for (GSList *p = prg->all_layers; p; p = g_slist_next(p))
    {
        struct sampler_layer *l = p->data;
        if (l->data.on_cc.is_active)
        {
            int cc = l->data.on_cc.cc_number;
            rll->layers_oncc = g_slist_prepend(rll->layers_oncc, l);
            rll->cc_trigger_bitmask[cc >> 5] |= 1 << (cc & 31);
        }
        else if (l->data.trigger == stm_release)
        {
            rll->layers_release = g_slist_prepend(rll->layers_release, l);
            if (l->data.lokey >= 0 && l->data.lokey <= 127 &&
                l->data.hikey >= 0 && l->data.hikey <= 127)
            {
                int start = rll->ranges_by_key[l->data.lokey];
                int end = rll->ranges_by_key[l->data.hikey];
                for (int i = start; i <= end; ++i)
                    rll->release_layers_by_range[i] = g_slist_prepend(rll->release_layers_by_range[i], l);
            }
        }
        else
        {
            rll->layers = g_slist_prepend(rll->layers, l);
            if (l->data.lokey >= 0 && l->data.lokey <= 127 &&
                l->data.hikey >= 0 && l->data.hikey <= 127)
            {
                int start = rll->ranges_by_key[l->data.lokey];
                int end = rll->ranges_by_key[l->data.hikey];
                for (int i = start; i <= end; ++i)
                    rll->layers_by_range[i] = g_slist_prepend(rll->layers_by_range[i], l);
            }
        }
    }
    return rll;
}

void sampler_rll_destroy(struct sampler_rll *rll)
{
    g_slist_free(rll->layers);
    g_slist_free(rll->layers_release);
    g_slist_free(rll->layers_oncc);
    for (uint32_t i = 0; i < rll->layers_by_range_count; ++i)
    {
        g_slist_free(rll->release_layers_by_range[i]);
        g_slist_free(rll->layers_by_range[i]);
    }
    free(rll->release_layers_by_range);
    free(rll->layers_by_range);
    free(rll);
}
