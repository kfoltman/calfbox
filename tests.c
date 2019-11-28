#include "module.h"
#include "engine.h"
#include "sampler.h"
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
        assert(module);
    }
    test_assert_equal_str(module->engine_name, "sampler");
    test_assert_equal_str(module->instance_name, instance_name);
    return (struct sampler_module *)module;
}

////////////////////////////////////////////////////////////////////////////////

void test_sampler_setup(struct test_env *env)
{
    struct sampler_module *m = create_sampler_instance(env, "test_setup", "smp1");
    
    test_assert(m);
    
    int count = 0;
    for (struct sampler_voice *v = m->voices_free; v; count++, v = v->next)
        test_assert(count < MAX_SAMPLER_VOICES);
    test_assert_equal(int, count, MAX_SAMPLER_VOICES);
    
    for (int i = 0; i < 16; ++i)
    {
        struct sampler_channel *c = &m->channels[i];
        test_assert(!c->voices_running);
    }
    
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
        cbox_config_close();
    }
    printf("%d tests run, %d tests failed.\n", tests_run, tests_failed);
    return tests_failed != 0;
}

