#include "app.h"
#include "blob.h"
#include "instr.h"
#include "rt.h"
#include "sampler.h"
#include "sampler_prg.h"

/////////////////////////////////////////////////////////////////////////////////

static void add_layers(struct sampler_rll *rll, GSList **layers_by_range, struct sampler_layer *l, uint32_t lokey, uint32_t hikey)
{
    if (lokey >= 0 && lokey <= 127 &&
        hikey >= 0 && hikey <= 127 && lokey <= hikey)
    {
        int start = rll->ranges_by_key[lokey];
        int end = rll->ranges_by_key[hikey];
        assert(start != 255 && end != 255);
        for (int i = start; i <= end; ++i)
        {
            if (!layers_by_range[i] || layers_by_range[i]->data != l)
                layers_by_range[i] = g_slist_prepend(layers_by_range[i], l);
        }
    }
}

struct sampler_rll *sampler_rll_new_from_program(struct sampler_program *prg)
{
    struct sampler_rll *rll = g_new(struct sampler_rll, 1);
    rll->layers_oncc = NULL;
    for (int i = 0; i < 4; i++)
        rll->cc_trigger_bitmask[i] = 0;

    GHashTable *keyswitch_groups = g_hash_table_new(g_direct_hash, g_direct_equal);
    uint32_t keyswitch_group_count = 0, keyswitch_key_count = 0;
    GPtrArray *keyswitch_group_array = g_ptr_array_new();
    memset(rll->ranges_by_key, 255, sizeof(rll->ranges_by_key));
    rll->has_release_layers = FALSE;
    for (GSList *p = prg->all_layers; p; p = g_slist_next(p))
    {
        struct sampler_layer *l = p->data;
        if (l->data.trigger == stm_release)
            rll->has_release_layers = TRUE;
        if (l->data.sw_last >= 0 && l->data.sw_last <= 127 &&
            l->data.sw_lokey >= 0 && l->data.sw_lokey <= 127 &&
            l->data.sw_hikey >= 0 && l->data.sw_hikey <= 127 &&
            l->data.sw_last >= l->data.sw_lokey && l->data.sw_last <= l->data.sw_hikey)
        {
            int width = l->data.sw_hikey - l->data.sw_lokey + 1;
            gpointer key = GINT_TO_POINTER(l->data.sw_lokey + (l->data.sw_hikey << 8));
            uint8_t value = l->data.sw_last - l->data.sw_lokey;
            struct sampler_keyswitch_group *ks = g_hash_table_lookup(keyswitch_groups, key);
            if (!ks)
            {
                ks = g_malloc(sizeof(struct sampler_keyswitch_group) + width);
                ks->lo = l->data.sw_lokey;
                ks->hi = l->data.sw_hikey;
                ks->num_used = 0;
                ks->def_value = 255;
                memset(ks->key_offsets, 255, width);

                g_hash_table_insert(keyswitch_groups, (gpointer)key, ks);
                g_ptr_array_add(keyswitch_group_array, ks);
                keyswitch_group_count++;
            }
            if (l->data.sw_default >= ks->lo && l->data.sw_default <= ks->hi && ks->def_value == 255)
                ks->def_value = l->data.sw_default - ks->lo;
            if (ks->key_offsets[value] == 255)
            {
                ks->key_offsets[value] = ks->num_used;
                ks->num_used++;
                keyswitch_key_count++;
                assert(ks->num_used <= width);
            }
        }
    }
    rll->keyswitch_groups = (gpointer)g_ptr_array_free(keyswitch_group_array, FALSE);
    rll->keyswitch_group_count = keyswitch_group_count;
    rll->keyswitch_key_count = keyswitch_key_count;
    uint32_t offset = 0;
    for (uint32_t i = 0; i < keyswitch_group_count; ++i)
    {
        rll->keyswitch_groups[i]->group_offset = 1 + offset;
        offset += rll->keyswitch_groups[i]->num_used;
    }
    assert(offset == keyswitch_key_count);

    uint16_t lo_count[129], hi_count[128], low = 127, high = 0;
    for (int i = 0; i < 128; i++)
        lo_count[i] = hi_count[i] = 0;
    lo_count[128] = 0;

    // XXXKF handle 'key' field without relying on the existing ugly hack
    for (GSList *p = prg->all_layers; p; p = g_slist_next(p))
    {
        struct sampler_layer *l = p->data;
        if (l->data.lokey >= 0 && l->data.lokey <= 127 &&
            l->data.hikey >= 0 && l->data.hikey <= 127 &&
            l->data.lokey <= l->data.hikey)
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
    rll->layers_by_range = g_malloc0_n(range_count * (1 + keyswitch_key_count), sizeof(GSList *));
    rll->release_layers_by_range = rll->has_release_layers ? g_malloc0_n(range_count * (1 + keyswitch_key_count), sizeof(GSList *)) : NULL;
    rll->layers_by_range_count = range_count;
    for (GSList *p = prg->all_layers; p; p = g_slist_next(p))
    {
        struct sampler_layer *l = p->data;
        uint32_t ks_offset = 0;
        if (l->data.sw_last >= 0 && l->data.sw_last <= 127 &&
            l->data.sw_lokey >= 0 && l->data.sw_lokey <= 127 &&
            l->data.sw_hikey >= 0 && l->data.sw_hikey <= 127 &&
            l->data.sw_last >= l->data.sw_lokey && l->data.sw_last <= l->data.sw_hikey)
        {
            gpointer key = GINT_TO_POINTER(l->data.sw_lokey + (l->data.sw_hikey << 8));
            struct sampler_keyswitch_group *ks = g_hash_table_lookup(keyswitch_groups, key);
            assert(ks);
            int rel_offset = ks->key_offsets[l->data.sw_last - l->data.sw_lokey];
            assert(rel_offset != -1);
            ks_offset = ks->group_offset + rel_offset;
        }

        struct sampler_cc_range *oncc = l->data.on_cc;
        if (oncc)
        {
            rll->layers_oncc = g_slist_prepend(rll->layers_oncc, l);
            while(oncc)
            {
                int cc = oncc->key.cc_number;
                rll->cc_trigger_bitmask[cc >> 5] |= 1 << (cc & 31);
                oncc = oncc->next;
            }
        }
        if (l->data.trigger == stm_release)
            add_layers(rll, rll->release_layers_by_range + ks_offset * range_count, l, l->data.lokey, l->data.hikey);
        else
            add_layers(rll, rll->layers_by_range + ks_offset * range_count, l, l->data.lokey, l->data.hikey);
    }
    g_hash_table_unref(keyswitch_groups);
    return rll;
}

void sampler_rll_destroy(struct sampler_rll *rll)
{
    g_slist_free(rll->layers_oncc);
    for (uint32_t i = 0; i < rll->layers_by_range_count * (1 + rll->keyswitch_key_count); ++i)
    {
        if (rll->has_release_layers)
            g_slist_free(rll->release_layers_by_range[i]);
        g_slist_free(rll->layers_by_range[i]);
    }
    for (uint32_t i = 0; i < rll->keyswitch_group_count; ++i)
        g_free(rll->keyswitch_groups[i]);
    g_free(rll->keyswitch_groups);
    g_free(rll->release_layers_by_range);
    g_free(rll->layers_by_range);
    g_free(rll);
}
