// Copyright (C) 2010 Krzysztof Foltman. All rights reserved.

#include <stdint.h>

#define CBOX_BLOCK_SIZE 16

typedef float cbox_sample_t;

struct cbox_module
{
    void *user_data;
    
    void (*process_event)(void *user_data, const uint8_t *data, uint32_t len);
    void (*process_block)(void *user_data, cbox_sample_t **inputs, cbox_sample_t **outputs);
};

struct cbox_module_manifest
{
    void *user_data;
    int inputs;
    int outputs;
    
    struct cbox_module *(*create)(void *user_data);
};

extern struct cbox_module_manifest mono_sine_module;
