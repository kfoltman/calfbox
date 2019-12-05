#include "module.h"
#include "engine.h"
#include "sampler.h"
#include "sfzloader.h"
#include "tests.h"

static struct sampler_module *create_sampler_instance(struct test_env *env, const char *cfg_section, const char *instance_name)
{
    extern struct cbox_module_manifest sampler_module;

    GError *error = NULL;
    struct cbox_module *module = cbox_module_manifest_create_module(&sampler_module, cfg_section, env->doc, NULL, env->engine, instance_name, &error);
    if (!module)
    {
        if (error)
            fprintf(stderr, "Error: %s\n", error->message);
        test_assert(module);
    }
    test_assert_equal_str(module->engine_name, "sampler");
    test_assert_equal_str(module->instance_name, instance_name);
    return (struct sampler_module *)module;
}

static int count_free_voices(struct test_env *env, struct sampler_module *m)
{
    int count = 0;
    for (struct sampler_voice *v = m->voices_free; v; count++, v = v->next)
        test_assert(count < MAX_SAMPLER_VOICES);
    return count;
}

static int count_channel_voices_if(struct test_env *env, struct sampler_module *m, int channel, gboolean (*cond_func)(struct sampler_voice *v, void *user_data), void *user_data)
{
    struct sampler_channel *c = &m->channels[channel];
    int count = 0;
    for (struct sampler_voice *v = c->voices_running; v; v = v->next)
    {
        test_assert(count < MAX_SAMPLER_VOICES);
        count += cond_func ? (cond_func(v, user_data) ? 1 : 0) : 1;
    }
    return count;
}

static void verify_sampler_voices_if(struct test_env *env, struct sampler_module *m, int voices[16], gboolean (*cond_func)(struct sampler_voice *v, void *user_data), void *user_data)
{
    int total = 0;
    for (int i = 0; i < 16; ++i)
    {
        int count = count_channel_voices_if(env, m, i, cond_func, user_data);
        test_assert_equal(int, count, voices[i]);
        total += count;
    }
    if (!cond_func)
        test_assert_equal(int, count_free_voices(env, m), MAX_SAMPLER_VOICES - total);
}

static void verify_sampler_voices(struct test_env *env, struct sampler_module *m, int voices[16])
{
    verify_sampler_voices_if(env, m, voices, NULL, NULL);
}

static gboolean is_voice_released(struct sampler_voice *v, void *ignore)
{
    return v->released;
}

static struct sampler_program *load_sfz_into_sampler(struct test_env *env, struct sampler_module *m, const char *sfz_data)
{
    GError *error = NULL;

    struct sampler_program *prg = sampler_program_new(m, 0, "note_test", NULL, NULL, &error);
    test_assert(prg);
    test_assert_no_error(error);

    test_assert(sampler_module_load_program_sfz(m, prg, sfz_data, 1, &error));
    test_assert_no_error(error);

    sampler_register_program(m, prg);
    test_assert(sampler_select_program(m, 0, prg->name, &error));
    test_assert_no_error(error);

    return prg;
}

////////////////////////////////////////////////////////////////////////////////

void test_sampler_setup(struct test_env *env)
{
    struct sampler_module *m = create_sampler_instance(env, "test_setup", "smp1");
    
    int expected_voices[16] = {};
    verify_sampler_voices(env, m, expected_voices);

    CBOX_DELETE(&m->module);
}

////////////////////////////////////////////////////////////////////////////////

void test_sampler_note_basic(struct test_env *env)
{
    struct sampler_module *m = create_sampler_instance(env, "test_setup", "smp1");
    struct sampler_program *prg = load_sfz_into_sampler(env, m,
        "<region> sample=*saw loop_mode=loop_continuous\n");

    for (int i = 0; i < 5; ++i)
    {
        uint8_t midi_data[3] = { 0x90, 48 + i, 127 };
        m->module.process_event(&m->module, midi_data, sizeof(midi_data));
        int expected_voices[16] = {[0] = 1 + i};
        verify_sampler_voices(env, m, expected_voices);
    }
    for (int i = 0; i < 5; ++i)
    {
        uint8_t midi_data[3] = { 0x91, 48 + i, 127 };
        m->module.process_event(&m->module, midi_data, sizeof(midi_data));
        int expected_voices[16] = {[0] = 5, [1] = 1 + i};
        verify_sampler_voices(env, m, expected_voices);
        int expected_released_voices[16] = {};
        verify_sampler_voices_if(env, m, expected_released_voices, is_voice_released, NULL);
    }

    // Send some MIDI off to the first channel
    for (int i = 0; i < 5; ++i)
    {
        uint8_t midi_data[3] = { (i & 1) ? 0x90 : 0x80, 48 + i, (i & 1) ? 0 : 127 };
        m->module.process_event(&m->module, midi_data, sizeof(midi_data));
        int expected_voices[16] = {[0] = 5, [1] = 5};
        verify_sampler_voices(env, m, expected_voices);
        int expected_released_voices[16] = {[0] = 1 + i};
        verify_sampler_voices_if(env, m, expected_released_voices, is_voice_released, NULL);
    }
    sampler_unselect_program(m, prg);
    CBOX_DELETE(prg);
    CBOX_DELETE(&m->module);
}

////////////////////////////////////////////////////////////////////////////////

struct region_logic_test_setup_step
{
    const uint8_t *midi_data;
    uint32_t midi_data_len;
    uint32_t voices[16];
};

struct region_logic_test_setup
{
    const char *name;
    const char *sfz_data;
    const struct region_logic_test_setup_step *steps;
};

void test_sampler_note_region_logic(struct test_env *env)
{
    struct region_logic_test_setup *setup = env->arg;
    struct sampler_module *m = create_sampler_instance(env, "test_setup", "smp1");
    struct sampler_program *prg = load_sfz_into_sampler(env, m, setup->sfz_data);

    int expected_voices[16] = {};
    for (int i = 0; setup->steps[i].midi_data; ++i)
    {
        env->context = g_strdup_printf("%s[%d]", setup->name, i);
        const struct region_logic_test_setup_step *step = &setup->steps[i];
        m->module.process_event(&m->module, step->midi_data, step->midi_data_len);
        for (int c = 0; c < 16; ++c)
            expected_voices[c] += step->voices[c];
        verify_sampler_voices(env, m, expected_voices);

        g_free(env->context);
        env->context = NULL;
    }
    sampler_unselect_program(m, prg);
    CBOX_DELETE(prg);
    CBOX_DELETE(&m->module);
}

////////////////////////////////////////////////////////////////////////////////

#define MIDI_DATA_STEP(data, voices) { (const uint8_t *)data, sizeof(data) - 1, {voices} }
#define MIDI_DATA_END { NULL, 0, {} }
#define MIDI_DATA_STEP_MT(data, ...) { (const uint8_t *)data, sizeof(data) - 1, {__VA_ARGS__} }
#define REGION_LOGIC_TEST_SETUP(_name, sfz) \
    struct region_logic_test_setup setup_##_name = { \
        .name = #_name, \
        .sfz_data = sfz, \
        .steps = steps_##_name \
    }

struct region_logic_test_setup_step steps_lokeyhikey[] = {
    MIDI_DATA_STEP("\x90\x24\x7F", 0),
    MIDI_DATA_STEP("\x90\x1F\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x7F", 1),
    MIDI_DATA_STEP("\x90\x21\x7F", 1),
    MIDI_DATA_STEP("\x90\x22\x7F", 1),
    MIDI_DATA_STEP("\x90\x23\x7F", 1),
    MIDI_DATA_END,
};

REGION_LOGIC_TEST_SETUP(lokeyhikey,
    "<region>lokey=32 hikey=35 sample=*saw"
);

struct region_logic_test_setup_step steps_lokeyhikey2[] = {
    MIDI_DATA_STEP("\x90\x0E\x7F", 0),
    MIDI_DATA_STEP("\x90\x0F\x7F", 1),
    MIDI_DATA_STEP("\x90\x10\x7F", 1),
    MIDI_DATA_STEP("\x90\x11\x7F", 0),
    MIDI_DATA_STEP("\x90\x1F\x7F", 0),
    MIDI_DATA_STEP("\x90\x24\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x7F", 1),
    MIDI_DATA_STEP("\x90\x21\x7F", 2),
    MIDI_DATA_STEP("\x90\x22\x7F", 2),
    MIDI_DATA_STEP("\x90\x23\x7F", 1),
    MIDI_DATA_STEP("\x90\x47\x7F", 0),
    MIDI_DATA_STEP("\x90\x48\x7F", 1),
    MIDI_DATA_STEP("\x90\x49\x7F", 0),
    MIDI_DATA_END,
};

REGION_LOGIC_TEST_SETUP(lokeyhikey2,
    "<region>lokey=15 hikey=16 sample=*saw\n"
    "<region>lokey=32 hikey=35 sample=*saw\n"
    "<region>lokey=33 hikey=34 sample=*saw\n"
    "<region>key=72 sample=*saw\n"
);

struct region_logic_test_setup_step steps_lovelhivel[] = {
    MIDI_DATA_STEP("\x90\x20\x1F", 0),
    MIDI_DATA_STEP("\x90\x20\x24", 0),
    MIDI_DATA_STEP("\x90\x20\x20", 1),
    MIDI_DATA_STEP("\x90\x20\x21", 1),
    MIDI_DATA_STEP("\x90\x20\x22", 1),
    MIDI_DATA_STEP("\x90\x20\x23", 1),
    MIDI_DATA_END,
};

REGION_LOGIC_TEST_SETUP(lovelhivel,
    "<region>lovel=32 hivel=35 sample=*saw"
);

struct region_logic_test_setup_step steps_lochanhichan[] = {
    MIDI_DATA_STEP_MT("\x90\x20\x7F", 0, 0, 0, 0, 0, 0, 0),
    MIDI_DATA_STEP_MT("\x91\x20\x7F", 0, 1, 0, 0, 0, 0, 0),
    MIDI_DATA_STEP_MT("\x92\x20\x7F", 0, 0, 1, 0, 0, 0, 0),
    MIDI_DATA_STEP_MT("\x93\x20\x7F", 0, 0, 0, 0, 0, 0, 0),
    MIDI_DATA_STEP_MT("\x94\x20\x7F", 0, 0, 0, 0, 2, 0, 0),
    MIDI_DATA_STEP_MT("\x95\x20\x7F", 0, 0, 0, 0, 0, 2, 0),
    MIDI_DATA_STEP_MT("\x96\x20\x7F", 0, 0, 0, 0, 0, 0, 0),
    MIDI_DATA_END,
};

REGION_LOGIC_TEST_SETUP(lochanhichan,
    "<region>lochan=2 hichan=3 sample=*saw "
    "<region>lochan=5 hichan=6 sample=*saw "
    "<region>lochan=5 hichan=6 sample=*saw "
);

struct region_logic_test_setup_step steps_chanaft[] = {
    MIDI_DATA_STEP("\x90\x20\x20", 0),
    MIDI_DATA_STEP("\xD0\x1F", 0),
    MIDI_DATA_STEP("\x90\x20\x20", 0),
    MIDI_DATA_STEP("\xD0\x20", 0),
    MIDI_DATA_STEP("\x90\x20\x20", 1),
    MIDI_DATA_STEP("\xD0\x21", 0),
    MIDI_DATA_STEP("\x90\x20\x20", 1),
    MIDI_DATA_STEP("\xD0\x22", 0),
    MIDI_DATA_STEP("\x90\x20\x20", 0),
    MIDI_DATA_END,
};

REGION_LOGIC_TEST_SETUP(chanaft,
    "<region>lochanaft=32 hichanaft=33 sample=*saw"
);

struct region_logic_test_setup_step steps_polyaft[] = {
    MIDI_DATA_STEP("\x90\x20\x20", 0),
    MIDI_DATA_STEP("\xA0\x10\x1F", 0),
    MIDI_DATA_STEP("\x90\x20\x20", 0),
    MIDI_DATA_STEP("\xA0\x10\x20", 0), // note that this does not care about which key - it uses the last poly aft value
    MIDI_DATA_STEP("\x90\x20\x20", 1),
    MIDI_DATA_STEP("\xA0\x10\x21", 0),
    MIDI_DATA_STEP("\x90\x20\x20", 1),
    MIDI_DATA_STEP("\xA0\x10\x22", 0),
    MIDI_DATA_STEP("\x90\x20\x20", 0),
    MIDI_DATA_END,
};

REGION_LOGIC_TEST_SETUP(polyaft,
    "<region>lopolyaft=32 hipolyaft=33 sample=*saw"
);

struct region_logic_test_setup_step steps_cc[] = {
    MIDI_DATA_STEP("\x90\x20\x20", 0),
    MIDI_DATA_STEP("\xB0\x10\x1F", 0),
    MIDI_DATA_STEP("\x90\x20\x20", 0),
    MIDI_DATA_STEP("\xB0\x10\x20", 0),
    MIDI_DATA_STEP("\x90\x20\x20", 1),
    MIDI_DATA_STEP("\xB0\x11\x7F", 0), // try a different CC, just in case (positive test)
    MIDI_DATA_STEP("\x90\x20\x20", 1),
    MIDI_DATA_STEP("\xB0\x10\x21", 0),
    MIDI_DATA_STEP("\x90\x20\x20", 1),
    MIDI_DATA_STEP("\xB0\x10\x22", 0),
    MIDI_DATA_STEP("\x90\x20\x20", 0),
    MIDI_DATA_STEP("\xB0\x11\x21", 0), // try a different CC, just in case (negative test)
    MIDI_DATA_STEP("\x90\x20\x20", 0),
    MIDI_DATA_STEP("\xB0\x10\x1F", 0),
    MIDI_DATA_STEP("\xB0\x11\x20", 0),
    MIDI_DATA_STEP("\x90\x20\x20", 0),
    MIDI_DATA_END,
};

REGION_LOGIC_TEST_SETUP(cc,
    "<region>locc16=32 hicc16=33 sample=*saw"
);

struct region_logic_test_setup_step steps_oncc[] = {
    MIDI_DATA_STEP("\xB0\x10\x1F", 0),
    MIDI_DATA_STEP("\xB0\x10\x20", 1),
    MIDI_DATA_STEP("\xB0\x10\x21", 1), // should probably be 1 according to test file 16, but that's madness
    MIDI_DATA_STEP("\xB0\x10\x22", 0),
    MIDI_DATA_STEP("\xB0\x10\x21", 1),
    MIDI_DATA_STEP("\xB0\x10\x20", 1), // same
    MIDI_DATA_STEP("\xB0\x10\x1F", 0),
    MIDI_DATA_END,
};

REGION_LOGIC_TEST_SETUP(oncc,
    "<region>on_locc16=32 on_hicc16=33 sample=*saw"
);

struct region_logic_test_setup_step steps_release[] = {
    MIDI_DATA_STEP("\x90\x20\x7F", 0),
    MIDI_DATA_STEP("\x80\x20\x7F", 1),
    MIDI_DATA_STEP("\x80\x20\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x00", 1),
    MIDI_DATA_STEP("\x90\x20\x00", 0),
    MIDI_DATA_END,
};

REGION_LOGIC_TEST_SETUP(release,
    "<region>trigger=release sample=*saw"
);

struct region_logic_test_setup_step steps_firstlegato[] = {
    MIDI_DATA_STEP_MT("\x90\x20\x7F", 1),
    MIDI_DATA_STEP_MT("\x90\x21\x7F", 2),
    MIDI_DATA_STEP_MT("\x90\x22\x7F", 2),
    MIDI_DATA_STEP_MT("\x91\x20\x7F", 0, 1), // a different channel has its own counter
    MIDI_DATA_STEP_MT("\x91\x21\x7F", 0, 2),
    MIDI_DATA_STEP_MT("\x91\x22\x7F", 0, 2),
    MIDI_DATA_STEP_MT("\x80\x20\x7F", 0),
    MIDI_DATA_STEP_MT("\x80\x21\x7F", 0),
    MIDI_DATA_STEP_MT("\x80\x22\x7F", 0),
    MIDI_DATA_STEP_MT("\x90\x20\x7F", 1),
    MIDI_DATA_STEP_MT("\x90\x21\x7F", 2),
    MIDI_DATA_STEP_MT("\x90\x22\x7F", 2),
    MIDI_DATA_END,
};

REGION_LOGIC_TEST_SETUP(firstlegato,
    "<region>trigger=first sample=*saw"
    "<region>trigger=legato sample=*saw"
    "<region>trigger=legato sample=*saw"
);

struct region_logic_test_setup_step steps_switches[] = {
    MIDI_DATA_STEP("\x90\x12\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x7F", 0),
    MIDI_DATA_STEP("\x90\x10\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x7F", 1),
    MIDI_DATA_STEP("\x90\x11\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x7F", 2),
    MIDI_DATA_STEP("\x90\x10\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x7F", 1),
    MIDI_DATA_STEP("\x90\x0F\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x7F", 1),
    MIDI_DATA_STEP("\x90\x14\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x7F", 1),
    MIDI_DATA_STEP("\x90\x12\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x7F", 0),
    MIDI_DATA_END,
};

REGION_LOGIC_TEST_SETUP(switches,
    "<region>sw_lokey=16 sw_hikey=19 sw_last=16 lokey=32 hikey=35 sample=*saw"
    "<region>sw_lokey=16 sw_hikey=19 sw_last=17 lokey=32 hikey=35 sample=*saw"
    "<region>sw_lokey=16 sw_hikey=19 sw_last=17 lokey=32 hikey=35 sample=*saw"
);

struct region_logic_test_setup_step steps_switches2[] = {
    MIDI_DATA_STEP("\x90\x20\x7F", 0),
    MIDI_DATA_STEP("\x90\x10\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x7F", 1),
    MIDI_DATA_STEP("\x80\x10\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x7F", 0),
    MIDI_DATA_STEP("\x90\x11\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x7F", 2),
    MIDI_DATA_STEP("\x80\x11\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x7F", 0),
    MIDI_DATA_STEP("\x90\x10\x7F", 0),
    MIDI_DATA_STEP("\x90\x11\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x7F", 3),
    MIDI_DATA_STEP("\x80\x10\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x7F", 2),
    MIDI_DATA_STEP("\x80\x11\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x7F", 0),
    MIDI_DATA_END,
};

REGION_LOGIC_TEST_SETUP(switches2,
    "<region>sw_lokey=16 sw_hikey=19 sw_down=16 lokey=32 hikey=35 sample=*saw"
    "<region>sw_lokey=16 sw_hikey=19 sw_down=17 lokey=32 hikey=35 sample=*saw"
    "<region>sw_lokey=16 sw_hikey=19 sw_down=17 lokey=32 hikey=35 sample=*saw"
);

struct region_logic_test_setup_step steps_switches3[] = {
    MIDI_DATA_STEP("\x90\x20\x7F", 2), // [0]
    MIDI_DATA_STEP("\x90\x12\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x7F", 1),
    MIDI_DATA_STEP("\x90\x10\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x7F", 2),
    MIDI_DATA_STEP("\x90\x11\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x7F", 3),
    MIDI_DATA_STEP("\x90\x10\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x7F", 2),
    MIDI_DATA_STEP("\x90\x0F\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x7F", 2), // [10]
    MIDI_DATA_STEP("\x90\x14\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x7F", 2),
    MIDI_DATA_STEP("\x90\x12\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x7F", 1),

    MIDI_DATA_STEP("\x90\x09\x7F", 0),
    MIDI_DATA_STEP("\x90\x12\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x7F", 2),
    MIDI_DATA_STEP("\x90\x10\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x7F", 3),
    MIDI_DATA_STEP("\x90\x11\x7F", 0), // [20]
    MIDI_DATA_STEP("\x90\x20\x7F", 4),
    MIDI_DATA_STEP("\x90\x10\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x7F", 3),
    MIDI_DATA_STEP("\x90\x0F\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x7F", 3),
    MIDI_DATA_STEP("\x90\x14\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x7F", 3),
    MIDI_DATA_STEP("\x90\x12\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x7F", 2),

    MIDI_DATA_STEP("\xB0\x79\x7F", 0), // [30] reset all controllers
    MIDI_DATA_STEP("\x90\x20\x7F", 2),
    MIDI_DATA_END,
};

REGION_LOGIC_TEST_SETUP(switches3,
    "<region>lokey=32 hikey=35 sample=*saw"
    "<region>sw_lokey=8 sw_hikey=9 sw_last=9 lokey=32 hikey=35 sample=*saw"
    "<region>sw_lokey=16 sw_hikey=19 sw_last=16 lokey=32 hikey=35 sample=*saw"
    "<region>sw_lokey=16 sw_hikey=19 sw_last=17 lokey=32 hikey=35 sample=*saw"
    "<region>sw_lokey=16 sw_hikey=19 sw_last=17 lokey=32 hikey=35 sample=*saw"
);

struct region_logic_test_setup_step steps_switches4[] = {
    MIDI_DATA_STEP("\x90\x20\x7F", 2),
    MIDI_DATA_STEP("\x90\x10\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x7F", 1),
    MIDI_DATA_STEP("\x90\x11\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x7F", 2),
    MIDI_DATA_STEP("\x90\x10\x7F", 0),
    MIDI_DATA_STEP("\xB0\x79\x7F", 0), // reset all controllers
    MIDI_DATA_STEP("\x90\x20\x7F", 2),
    MIDI_DATA_STEP("\x90\x08\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x7F", 3),
    MIDI_DATA_STEP("\x90\x09\x7F", 0),
    MIDI_DATA_STEP("\x90\x20\x7F", 4),
    MIDI_DATA_STEP("\xB0\x79\x7F", 0), // reset all controllers
    MIDI_DATA_STEP("\x90\x20\x7F", 2),
    MIDI_DATA_END,
};

REGION_LOGIC_TEST_SETUP(switches4,
    "<region>sw_lokey=16 sw_hikey=19 sw_default=17 sw_last=16 lokey=32 hikey=35 sample=*saw"
    "<region>sw_lokey=16 sw_hikey=19 sw_last=17 lokey=32 hikey=35 sample=*saw"
    "<region>sw_lokey=16 sw_hikey=19 sw_last=17 lokey=32 hikey=35 sample=*saw"
    "<region>sw_lokey=8 sw_hikey=10 sw_last=8 lokey=32 hikey=35 sample=*saw"
    "<region>sw_lokey=8 sw_hikey=10 sw_last=9 lokey=32 hikey=35 sample=*saw"
    "<region>sw_lokey=8 sw_hikey=10 sw_default=10 sw_last=9 lokey=32 hikey=35 sample=*saw"
);

////////////////////////////////////////////////////////////////////////////////

void test_assert_failed(struct test_env *env, const char *file, int line, const char *check)
{
    if (env->context)
        fprintf(stderr, "FAIL @%s:%d Assertion '%s' failed, context: %s.\n", file, line, check, env->context);
    else
        fprintf(stderr, "FAIL @%s:%d Assertion '%s' failed.\n", file, line, check);
    longjmp(env->on_fail, 1);
}

void test_assert_failed_free(struct test_env *env, const char *file, int line, gchar *check)
{
    if (env->context)
        fprintf(stderr, "FAIL @%s:%d %s, context: %s\n", file, line, check, env->context);
    else
        fprintf(stderr, "FAIL @%s:%d %s.\n", file, line, check);
    g_free(check);
    longjmp(env->on_fail, 1);
}

////////////////////////////////////////////////////////////////////////////////

struct test_info {
    const char *name;
    void (*func)(struct test_env *env);
    void *arg;
} tests[] = {
    { "test_sampler_setup", test_sampler_setup },
    { "test_sampler_note_basic", test_sampler_note_basic },
    { "test_sampler_note_region_logic/key", test_sampler_note_region_logic, &setup_lokeyhikey },
    { "test_sampler_note_region_logic/key2", test_sampler_note_region_logic, &setup_lokeyhikey2 },
    { "test_sampler_note_region_logic/vel", test_sampler_note_region_logic, &setup_lovelhivel },
    { "test_sampler_note_region_logic/ch", test_sampler_note_region_logic, &setup_lochanhichan },
    { "test_sampler_note_region_logic/chanaft", test_sampler_note_region_logic, &setup_chanaft },
    { "test_sampler_note_region_logic/polyaft", test_sampler_note_region_logic, &setup_polyaft },
    { "test_sampler_note_region_logic/cc", test_sampler_note_region_logic, &setup_cc },
    { "test_sampler_note_region_logic/oncc", test_sampler_note_region_logic, &setup_oncc },
    { "test_sampler_note_region_logic/release", test_sampler_note_region_logic, &setup_release },
    { "test_sampler_note_region_logic/firstlegato", test_sampler_note_region_logic, &setup_firstlegato },
    { "test_sampler_note_region_logic/switches", test_sampler_note_region_logic, &setup_switches },
    { "test_sampler_note_region_logic/switches2", test_sampler_note_region_logic, &setup_switches2 },
    { "test_sampler_note_region_logic/switches3", test_sampler_note_region_logic, &setup_switches3 },
    { "test_sampler_note_region_logic/switches4", test_sampler_note_region_logic, &setup_switches4 },
};

int main(int argc, char *argv[])
{
    uint32_t tests_run = 0, tests_failed = 0;
    for (unsigned int i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i)
    {
        struct test_env env;
        env.doc = cbox_document_new();
        env.engine = cbox_engine_new(env.doc, NULL);
        env.arg = tests[i].arg;
        env.context = NULL;
        cbox_config_init("");
        cbox_wavebank_init();
        tests_run++;
        if (0 == setjmp(env.on_fail))
        {
            printf("Running %s... ", tests[i].name);
            fflush(stdout);
            tests[i].func(&env);
            printf("PASS\n");
        }
        else
            tests_failed++;

        CBOX_DELETE(env.engine);
        env.engine = NULL;
        cbox_document_destroy(env.doc);
        env.doc = NULL;
        cbox_wavebank_close();
        cbox_config_close();
        if (env.context)
        {
            g_free(env.context);
            env.context = NULL;
        }
    }
    printf("%d tests ran, %d tests failed.\n", tests_run, tests_failed);
    return tests_failed != 0;
}

