/*
Calf Box, an open source musical instrument.
Copyright (C) 2010-2013 Krzysztof Foltman

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

#include "master.h"
#include "seq.h"

struct cbox_adhoc_pattern *cbox_adhoc_pattern_new(struct cbox_engine *engine, int id, struct cbox_midi_pattern *pattern)
{
    struct cbox_adhoc_pattern *ap = calloc(1, sizeof(struct cbox_adhoc_pattern));
    ap->next = NULL;
    ap->pattern = pattern;
    ap->pattern_playback = cbox_midi_pattern_playback_new(pattern);
    ap->master = cbox_master_new(engine);
    cbox_midi_playback_active_notes_init(&ap->active_notes);
    cbox_midi_clip_playback_init(&ap->playback, &ap->active_notes, ap->master);
    cbox_midi_buffer_init(&ap->output_buffer);
    ap->id = id;
    ap->completed = FALSE;
    
    return ap;
}

void cbox_adhoc_pattern_render(struct cbox_adhoc_pattern *ap, uint32_t offset, uint32_t nsamples)
{
    if (ap->completed)
    {
        cbox_midi_playback_active_notes_release(&ap->active_notes, &ap->output_buffer, NULL);
        return;
    }
    if (ap->playback.pos >= ap->playback.pattern->event_count)
        ap->completed = TRUE;
    cbox_midi_clip_playback_render(&ap->playback, &ap->output_buffer, offset, nsamples, FALSE);
}

void cbox_adhoc_pattern_destroy(struct cbox_adhoc_pattern *ap)
{
    // XXXKF decide on pattern ownership and general object lifetime issues
    cbox_midi_pattern_playback_destroy(ap->playback.pattern);
    cbox_master_destroy(ap->master);
    free(ap);
}
