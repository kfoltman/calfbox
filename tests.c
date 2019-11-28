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

static void verify_sampler_voices(struct test_env *env, struct sampler_module *m, int voices[16])
{
    int total = 0;
    for (int i = 0; i < 16; ++i)
    {
        struct sampler_channel *c = &m->channels[i];
        int count = 0;
        for (struct sampler_voice *v = c->voices_running; v; count++, v = v->next)
            test_assert(count < MAX_SAMPLER_VOICES);
        test_assert_equal(int, count, voices[i]);
        total += count;
    }
    test_assert_equal(int, count_free_voices(env, m), MAX_SAMPLER_VOICES - total);
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

void test_sampler_note(struct test_env *env)
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
    }

    sampler_unselect_program(m, prg);
    CBOX_DELETE(prg);
    CBOX_DELETE(&m->module);
}

////////////////////////////////////////////////////////////////////////////////

void test_assert_failed(struct test_env *env, const char *file, int line, const char *check)
{
    fprintf(stderr, "FAIL @%s:%d Assertion '%s' failed.\n", file, line, check);
    longjmp(env->on_fail, 1);
}

void test_assert_failed_free(struct test_env *env, const char *file, int line, gchar *check)
{
    fprintf(stderr, "FAIL @%s:%d %s.\n", file, line, check);
    g_free(check);
    longjmp(env->on_fail, 1);
}

////////////////////////////////////////////////////////////////////////////////

struct test_info {
    const char *name;
    void (*func)(struct test_env *env);
} tests[] = {
    { "test_sampler_setup", test_sampler_setup },
    { "test_sampler_note", test_sampler_note },
};

int main(int argc, char *argv[])
{
    uint32_t tests_run = 0, tests_failed = 0;
    for (unsigned int i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i)
    {
        struct test_env env;
        env.doc = cbox_document_new();
        env.engine = cbox_engine_new(env.doc, NULL);
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
    }
    printf("%d tests ran, %d tests failed.\n", tests_run, tests_failed);
    return tests_failed != 0;
}

