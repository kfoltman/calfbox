#include "app.h"
#include "blob.h"
#include "instr.h"
#include "rt.h"
#include "sampler.h"
#include "sampler_prg.h"

/////////////////////////////////////////////////////////////////////////////////

static void add_layers(struct sampler_rll *rll, GSList **layers, GSList **layers_by_range, struct sampler_layer *l, uint32_t lokey, uint32_t hikey)
{
    if (lokey >= 0 && lokey <= 127 &&
        hikey >= 0 && hikey <= 127)
    {
        if (!*layers || (*layers)->data != l)
            *layers = g_slist_prepend(*layers, l);
        int start = rll->ranges_by_key[lokey];
        int end = rll->ranges_by_key[hikey];
        for (int i = start; i <= end; ++i)
        {
            if (!layers_by_range[i] || layers_by_range[i]->data != l)
                layers_by_range[i] = g_slist_prepend(layers_by_range[i], l);
        }
    }
}

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
        if (l->data.lokey >= 0 && l->data.lokey <= 127 &&
            l->data.hikey >= 0 && l->data.hikey <= 127)
        {
            lo_count[l->data.lokey]++;
            hi_count[l->data.hikey]++;
            if (l->data.lokey < low)
                low = l->data.lokey;
            if (l->data.hikey > high)
                high = l->data.hikey;
            if (l->data.sw_last != -1)
            {
                if (l->data.sw_lokey < low)
                    low = l->data.sw_lokey;
                if (l->data.sw_hikey > high)
                    high = l->data.sw_hikey;
            }
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
        if (l->data.trigger == stm_release)
            add_layers(rll, &rll->layers_release, rll->release_layers_by_range, l, l->data.lokey, l->data.hikey);
        else
            add_layers(rll, &rll->layers, rll->layers_by_range, l, l->data.lokey, l->data.hikey);
        // Add key switches (add_layers avoids adding the duplicates).
        if (l->data.sw_last != -1)
            add_layers(rll, &rll->layers, rll->layers_by_range, l, l->data.sw_lokey, l->data.sw_hikey);
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
