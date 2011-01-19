// Copyright (C) 2010 Krzysztof Foltman. All rights reserved.

#include <jack/jack.h>

struct cbox_io_callbacks;

struct cbox_open_params
{
};

struct cbox_io
{
    jack_client_t *client;
    jack_port_t *output_l, *output_r;
    jack_port_t *midi;
    
    struct cbox_io_callbacks *cb;
};

struct cbox_io_callbacks
{
    void *user_data;
    
    void (*process)(void *user_data, struct cbox_io *io, uint32_t nframes);
};

extern int cbox_io_init(struct cbox_io *io, struct cbox_open_params *const params);
extern int cbox_io_start(struct cbox_io *io, struct cbox_io_callbacks *cb);
extern int cbox_io_stop(struct cbox_io *io);
extern void cbox_io_close(struct cbox_io *io);
