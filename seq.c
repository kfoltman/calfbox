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

#include "pattern.h"
#include "seq.h"
#include "track.h"

struct cbox_track_playback *cbox_track_playback_new_from_track(struct cbox_track *track, int pos)
{
    struct cbox_track_playback *pb = malloc(sizeof(struct cbox_track_playback));
    int len = g_list_length(track->items);
    pb->items = malloc(len * sizeof(struct cbox_track_playback_item *));
    
    GList *it = track->items;
    struct cbox_track_playback_item **p = pb->items;
    uint32_t safe = 0;
    while(it != NULL)
    {
        struct cbox_track_item *item = it->data;
        // if items overlap, the first one takes precedence
        if (item->time < safe)
        {
            // fully contained in previous item? skip all of it
            // not fully contained - insert the fragment
            if (item->time + item->length >= safe)
            {
                int cut = safe - item->time;
                (*p)->time = safe;
                (*p)->pattern = item->pattern;
                (*p)->offset = item->offset + cut;
                (*p)->length = item->length - cut;
                p++;
            }
        }
        else
        {
            (*p)->time = item->time;
            (*p)->pattern = item->pattern;
            (*p)->offset = item->offset;
            (*p)->length = item->length;
            safe = item->time + item->length;
            p++;
        }
        
        it = g_list_next(it);
    }
    // in case of full overlap, some items might have been skipped
    pb->items_count = p - pb->items;
    
    pb->pos = 0;
    while(pb->pos < pb->items_count && pb->items[pos]->time + pb->items[pos]->length < pos)
        pb->pos++;
    cbox_midi_playback_active_notes_init(&pb->active_notes);
    
    return pb;
}

void cbox_track_playback_destroy(struct cbox_track_playback *pb)
{
    free(pb->items);
    free(pb);
}
