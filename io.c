// Copyright (C) 2010 Krzysztof Foltman. All rights reserved.

#include "io.h"

int cbox_io_init(struct cbox_io *io, struct cbox_open_params *const params)
{
    jack_status_t status = 0;
    io->client = jack_client_open("cbox", 0, &status);
    io->cb = NULL;
    
    if (io->client == NULL)
        return 0;

    io->output_l = jack_port_register(io->client, "out_l", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    io->output_r = jack_port_register(io->client, "out_r", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    io->midi = jack_port_register(io->client, "midi", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
    
    if (!io->output_l || !io->output_r || !io->midi)
        return 0;
    
    return 1;
};

static int process_cb(jack_nframes_t frames, void *arg)
{
    struct cbox_io *io = arg;
    struct cbox_io_callbacks *cb = io->cb;
    
    cb->process(cb->user_data, io, frames);
    return 0;
}

int cbox_io_start(struct cbox_io *io, struct cbox_io_callbacks *cb)
{
    io->cb = cb;
    jack_set_process_callback(io->client, process_cb, io);
    jack_activate(io->client);

    jack_connect(io->client, "cbox:out_l", "system:playback_1");
    jack_connect(io->client, "cbox:out_r", "system:playback_2");
    jack_connect(io->client, "alsa_pcm:E-MU-Xboard25/midi_capture_1", "cbox:midi");
    jack_connect(io->client, "alsa_pcm:E-MU-XMidi2X2/midi_capture_2", "cbox:midi");
    
    return 1;
}

int cbox_io_stop(struct cbox_io *io)
{
    jack_deactivate(io->client);
    return 1;
}

void cbox_io_close(struct cbox_io *io)
{
    if (io->client)
    {
        if (io->output_l)
            jack_port_unregister(io->client, io->output_l);
        if (io->output_r)
            jack_port_unregister(io->client, io->output_r);
        if (io->midi)
            jack_port_unregister(io->client, io->midi);
        
        jack_client_close(io->client);
    }
}

