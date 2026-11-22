/*+===================================================================
  File:      option_help_test.c

  Summary:   Implements option help test support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "rsys/common.h"
#include <assert.h>
#include <setjmp.h>

struct allocation {
    size_t size;
    unsigned guard;
};

static int fail_after = -1, live;
static jmp_buf allocation_error;
char* program_name = "option-test";

static void check(struct allocation* p)
{
    assert(p->guard == 0xA5A55A5A);
    assert(!memcmp((char*)(p + 1) + p->size, "GUARD", 5));
}

static void* checked_realloc(void* old, size_t size)
{
    struct allocation* p = old ? (struct allocation*)old - 1 : NULL;
    if (p)
        check(p);
    if (!fail_after)
        return NULL;
    if (fail_after > 0)
        --fail_after;
    if (!p)
        ++live;
    p = realloc(p, sizeof *p + size + 5);
    assert(p);
    p->size = size;
    p->guard = 0xA5A55A5A;
    memcpy((char*)(p + 1) + size, "GUARD", 5);
    return p + 1;
}

static void* checked_malloc(size_t size)
{
    return checked_realloc(NULL, size);
}

static void checked_free(void* old)
{
    if (old) {
        struct allocation* p = (struct allocation*)old - 1;
        check(p);
        --live;
        free(p);
    }
}

static void allocation_exit(int status)
{
    assert(status == -16);
    longjmp(allocation_error, 1);
}

#define malloc checked_malloc
#define realloc checked_realloc
#define free checked_free
#define exit allocation_exit
#include "../../options/option.c"
#undef malloc
#undef realloc
#undef free
#undef exit
#undef main

int main(void)
{
    option_t options[12];
    char names[12][20];
    char* large = malloc(200001);
    int i, attempt, completed = 0;
    assert(large);
    memset(large, 'Q', 200000);
    large[200000] = 0;
    for (i = 0; i < 12; ++i) {
        sprintf(names[i], "option%d", i);
        options[i] = (option_t){names[i], i ? "short description" : large, opt_no_arg, NULL};
    }
    /* Force every allocation in registration and help generation to fail in turn. */
    for (attempt = 0; attempt < 100; ++attempt) {
        fail_after = attempt;
        if (!setjmp(allocation_error)) {
            for (i = 0; i < 12; ++i) {
                opt_register_pre_note("A normal note to wrap and retain.");
                opt_register("test interface", &options[i], 1);
            }
            assert(opt_help_message() && strstr(opt_help_message(), "-option11"));
            assert(strlen(opt_help_message()) == help_buf_len);
            completed = 1;
        }
        fail_after = -1;
        opt_shutdown();
        opt_shutdown();
        assert(!live);
        if (completed)
            break;
    }
    assert(completed && attempt > 10);
    /* An append larger than twice the current capacity must grow far enough. */
    send_to_help_buf("", 0, TRUE);
    send_to_help_buf(large, 200000, TRUE);
    assert(help_buf_len == 200002 && strlen(help_buf) == help_buf_len);
    assert(help_buf[0] == '\n' && help_buf[200001] == '\n');
    opt_shutdown();
    {
        opt_database_t* db = opt_alloc_db();
        for (i = 0; i < 12; ++i)
            opt_put_val(db, names[i], "value", pri_command_line, FALSE);
        assert(db->n_opt_vals == 12);
        checked_free(db->opt_vals);
        checked_free(db);
    }
    free(large);
    assert(!live);
    puts("PASS: long help words, large appends, allocation failures, registry growth and repeated shutdown");
    return 0;
}
