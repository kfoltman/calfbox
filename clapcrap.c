#include <clap/entry.h>
#include <clap/plugin.h>
#include <clap/factory/plugin-factory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "config-api.h"
#include "engine.h"
#include "instr.h"
#include "layer.h"
#include "sampler.h"
#include "scene.h"
#include "tarfile.h"
#include "wavebank.h"

struct plugin_instance {
    struct cbox_rt *rt;
    struct cbox_engine *engine;
};

static const char *const plugin_features[] = {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SAMPLER,
    CLAP_PLUGIN_FEATURE_STEREO,
    NULL
};

static clap_plugin_descriptor_t plugin_descriptor = {
    .clap_version = CLAP_VERSION,
    .id = "calfbox.foltman.com",
    .name = "Calfbox",
    .vendor = "Krzysztof Foltman",
    .url = "http://github.com/kfoltman/calfbox.git",
    .manual_url = "",
    .support_url = "",
    .version = "0.0.1",
    .description = "Just a test for creating a plugin",
    .features = plugin_features,
};

void sampler_process_event(struct cbox_module *module, const uint8_t *data, uint32_t len);

bool CLAP_ABI plugin_init(const clap_plugin_t *plugin)
{
    struct plugin_instance *pinst = plugin->plugin_data;
    GError *error = NULL;
    pinst->rt = cbox_rt_new(app.document);
    pinst->engine = cbox_engine_new(app.document, pinst->rt);
    pinst->engine->io_env.srate = 44100;
    pinst->engine->io_env.buffer_size = 128;
    struct cbox_scene *scene = cbox_scene_new(app.document, pinst->engine);
    assert(scene);
    struct cbox_instrument *instr = cbox_scene_create_instrument(scene, "organ", "sampler", &error);
    if (!instr) {
        printf("%s\n", error->message);
    }
    struct cbox_layer *layer = cbox_layer_new_with_instrument(scene, "organ", &error);
    if (!layer) {
        printf("%s\n", error->message);
    }
    if (!cbox_scene_add_layer(scene, layer, &error)) {
        printf("%s\n", error->message);
    }
    struct sampler_program *pgm;
    if (!load_program_at((struct sampler_module *)instr->module, "spgm:!synthbass.sfz", "synthbass", 0, &pgm, &error)) {
        printf("%s\n", error->message);
    }
    sampler_process_event(instr->module, "\x90\x24\x7F", 3);
    return instr != NULL;
}

void CLAP_ABI plugin_destroy(const clap_plugin_t *plugin)
{
}

bool CLAP_ABI plugin_activate(const struct clap_plugin *plugin,
                        double                    sample_rate,
                        uint32_t                  min_frames_count,
                        uint32_t                  max_frames_count)
{
    struct plugin_instance *pinst = plugin->plugin_data;
    printf("%d..%d\n", (int)min_frames_count, (int)max_frames_count);
    cbox_rt_set_offline(pinst->rt, sample_rate, max_frames_count);
    return 1;
}

void CLAP_ABI plugin_deactivate(const clap_plugin_t *plugin)
{
}

bool CLAP_ABI plugin_start_processing(const clap_plugin_t *plugin)
{
    return 1;
}

void CLAP_ABI plugin_stop_processing(const clap_plugin_t *plugin)
{
}

void CLAP_ABI plugin_reset(const clap_plugin_t *plugin)
{
}

clap_process_status plugin_process(const struct clap_plugin *plugin, const clap_process_t *process)
{
    struct plugin_instance *pinst = plugin->plugin_data;
    cbox_rt_handle_rt_commands(pinst->rt);
    assert(process->audio_outputs[0].channel_count == 2);
    float *buffers[2] = {process->audio_outputs[0].data32[0], process->audio_outputs[0].data32[1]};
    for (uint32_t i = 0; i < process->frames_count; ++i)
        buffers[0][i] = buffers[1][i] = 0.f;
    cbox_engine_process(pinst->engine, NULL, process->frames_count, buffers, 2);
    return CLAP_PROCESS_CONTINUE;
}

clap_plugin_t plugin = {
    .desc = &plugin_descriptor,
    .plugin_data = NULL,
    .init = plugin_init,
    .destroy = plugin_destroy,
    .activate = plugin_activate,
    .deactivate = plugin_deactivate,
    .start_processing = plugin_start_processing,
    .stop_processing = plugin_stop_processing,
    .reset = plugin_reset,
    .process = plugin_process,
    .get_extension = NULL,
    .on_main_thread = NULL,
};


uint32_t CLAP_ABI get_plugin_count(const struct clap_plugin_factory *factory) {
    return 1;
}

const clap_plugin_descriptor_t *CLAP_ABI get_plugin_descriptor(const struct clap_plugin_factory *factory, uint32_t index)
{
    if (index == 0)
        return &plugin_descriptor;
    return NULL;
}

const clap_plugin_t *CLAP_ABI create_plugin(const struct clap_plugin_factory *factory, const clap_host_t *host, const char *plugin_id)
{
    clap_plugin_t *p = calloc(1, sizeof(clap_plugin_t));
    memcpy(p, &plugin, sizeof(plugin));
    p->plugin_data = calloc(1, sizeof(struct plugin_instance));
    return p;
}

clap_plugin_factory_t plugin_factory = {
    .get_plugin_count = get_plugin_count,
    .get_plugin_descriptor = get_plugin_descriptor,
    .create_plugin = create_plugin,
};

const void *CLAP_ABI get_factory(const char *factory_id) {
    if (!strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID))
        return &plugin_factory;
    return NULL;
}

bool CLAP_ABI clap_init(const char *plugin_path) {
    fprintf(stderr, "clap_init %s\n", plugin_path);
    app.tarpool = cbox_tarpool_new();
    app.document = cbox_document_new();
    app.rt = NULL;
    app.engine = NULL;
    cbox_config_init("");
#if 0
    app.rt = cbox_rt_new(app.document);
    app.engine = cbox_engine_new(app.document, app.rt);
    app.rt->engine = app.engine;
    cbox_rt_set_offline(app.rt, no_io_srate, 1024);
#endif
    cbox_wavebank_init();
    return 1;
}

void CLAP_ABI clap_deinit() {
}

CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION,
    .init = clap_init,
    .deinit = clap_deinit,
    .get_factory = get_factory
};