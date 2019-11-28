#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <setjmp.h>

struct test_env
{
    struct cbox_document *doc;
    struct cbox_engine *engine;
    jmp_buf on_fail;
};

#define test_assert(condition) \
    if (!(condition)) \
        test_assert_failed(env, __FILE__, __LINE__, #condition);

#define STR_FORMAT_int "%d"
#define STR_FORMAT_unsigned "%u"
#define STR_FORMAT_uint32_t PRIu32

#define test_assert_equal(type, val1, val2) \
    do { \
        type _v1 = (val1), _v2 = (val2); \
        if ((_v1) != (_v2)) \
            test_assert_failed(env, __FILE__, __LINE__, g_strdup_printf(STR_FORMAT_##type " != " STR_FORMAT_##type, _v1, _v2)); \
    } while(0);

#define test_assert_equal_str(val1, val2) \
    do { \
        const char *_v1 = (val1), *_v2 = (val2); \
        if (strcmp(_v1, _v2)) \
            test_assert_failed_free(env, __FILE__, __LINE__, g_strdup_printf("%s equal to '%s', not '%s'", #val1, _v1, _v2)); \
    } while(0);

extern void test_assert_failed(struct test_env *env, const char *file, int line, const char *check);
extern void test_assert_failed_free(struct test_env *env, const char *file, int line, gchar *check);

