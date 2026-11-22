/*+===================================================================
  File:      option.c

  Summary:   Implements option support for ROMAW options and preferences.

  Classes:   None.

  Functions: opt_alloc_db, opt_help_message, opt_init, opt_int_val, opt_lookup, opt_parse, opt_put_int_val, opt_put_val,
             opt_register, opt_register_pre_note, opt_shutdown, opt_val

  Origin:    Existing repository source in executor/src/options; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1995, 1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

#include "rsys/flags.h"
#include "rsys/option.h"
#include "rsys/parsenum.h"
#include "rsys/notmac.h"

struct opt_block {
    char* interface;
    option_t* opts;
    int n_opts;
};
static struct opt_block* opt_blocks;
static int n_opt_blocks_max;
static int n_opt_blocks;

static int max_pre_notes;
static int n_pre_notes;
static char** pre_notes;

static char* wrap_buf;
static size_t wrap_buf_size;

static size_t help_buf_len, help_buf_len_max;
static char* help_buf;

static void option_memory_error(void)
{
    fputs("Unable to allocate command-line options.\n", stderr);
    exit(-16);
}

static void* option_malloc(size_t size)
{
    void* result = malloc(size);
    if (!result)
        option_memory_error();
    return result;
}

static void* option_realloc(void* old, size_t size)
{
    void* result = realloc(old, size);
    if (!result)
        option_memory_error();
    return result;
}

static void* grow_option_array(void* old, int* capacity, size_t element_size)
{
    int next;
    if (*capacity > INT_MAX / 2)
        option_memory_error();
    next = *capacity ? *capacity * 2 : 4;
    if ((size_t)next > (size_t)-1 / element_size)
        option_memory_error();
    old = option_realloc(old, next * element_size);
    *capacity = next;
    return old;
}

void opt_init(void)
{
}

void opt_shutdown(void)
{
    free(wrap_buf);
    free(help_buf);
    free(pre_notes);
    free(opt_blocks);
    wrap_buf = help_buf = NULL;
    pre_notes = NULL;
    opt_blocks = NULL;
    wrap_buf_size = help_buf_len = help_buf_len_max = 0;
    n_pre_notes = max_pre_notes = n_opt_blocks = n_opt_blocks_max = 0;
}

static void strcpy_to_wrap_buf(char* text)
{
    size_t text_len;
    char *src, *dst;

    text_len = strlen(text) + 1;

    if (wrap_buf_size < text_len) {
        wrap_buf = option_realloc(wrap_buf, text_len * sizeof *wrap_buf);
        wrap_buf_size = text_len;
    }
    for (dst = wrap_buf, src = text; *src;) {
        if (*src != '\n')
            *dst++ = *src++;
        else
            src++;
    }
    *dst = '\0';
}

static void wrap(char* buf, int desired_len, int* out_len, int* next_len)
{
    size_t buf_len = strlen(buf);
    int split;

    if (buf_len <= desired_len) {
        *out_len = buf_len;
        *next_len = -1;
        return;
    }
    split = desired_len;
    while (split > 0 && !isspace((unsigned char)buf[split]))
        --split;
    if (!split) {
        /* A word longer than the line must be split without walking before buf. */
        *out_len = *next_len = desired_len;
        return;
    }
    *next_len = split;
    while (buf[*next_len] && isspace((unsigned char)buf[*next_len]))
        ++*next_len;
    while (split > 0 && isspace((unsigned char)buf[split - 1]))
        --split;
    *out_len = split;
}

static void send_to_help_buf(const char* text, size_t len, int append_newline_p)
{
    size_t terminators = append_newline_p ? 2 : 1;
    size_t needed;

    if (len > (size_t)-1 - terminators || help_buf_len > (size_t)-1 - len - terminators)
        option_memory_error();
    needed = help_buf_len + len + terminators;
    if (needed > help_buf_len_max) {
        size_t capacity = help_buf_len_max ? help_buf_len_max : 128;
        while (capacity < needed) {
            if (capacity > (size_t)-1 / 2) {
                capacity = needed;
                break;
            }
            capacity *= 2;
        }
        help_buf = option_realloc(help_buf, capacity);
        help_buf_len_max = capacity;
    }
    memcpy(help_buf + help_buf_len, text, len);
    help_buf_len += len;
    if (append_newline_p)
        help_buf[help_buf_len++] = '\n';
    help_buf[help_buf_len] = 0;
}

static void _generate_help_message(void)
{
    int i, block_i;
    char* buf;

    for (i = 0; i < n_pre_notes; i++) {
        int out_len, next_len;
        char* pre_note = pre_notes[i];

        strcpy_to_wrap_buf(pre_note);

        for (buf = wrap_buf;; buf += next_len) {
            wrap(buf, 75, &out_len, &next_len);
            send_to_help_buf(buf, out_len, TRUE);
            if (next_len == -1)
                break;
        }
    }
    /* newline to separate pre-notes and options */
    send_to_help_buf("", 0, TRUE);
    for (block_i = 0; block_i < n_opt_blocks; block_i++) {
        char* interface = opt_blocks[block_i].interface;
        option_t* opts = opt_blocks[block_i].opts;
        int n_opts = opt_blocks[block_i].n_opts;
        int opt_i;

        send_to_help_buf(interface, strlen(interface), FALSE);
        send_to_help_buf(":", 1, TRUE);

        for (opt_i = 0; opt_i < n_opts; opt_i++) {
            char* spaces = "                    ";
            int next_len, out_len;

            option_t* opt = &opts[opt_i];
            int opt_text_len;
            int same_line_p;

            if (!opt->desc)
                continue;

            opt_text_len = strlen(opt->text);
            same_line_p = opt_text_len < 14;

            send_to_help_buf("  -", 3, FALSE);
            send_to_help_buf(opt->text, opt_text_len, !same_line_p);
            if (same_line_p)
                send_to_help_buf(spaces, 20 - (3 + opt_text_len), FALSE);
            else
                send_to_help_buf(spaces, 20, FALSE);

            strcpy_to_wrap_buf(opt->desc);
            for (buf = wrap_buf;; buf += next_len) {
                wrap(buf, 55, &out_len, &next_len);
                send_to_help_buf(buf, out_len, TRUE);
                if (next_len != -1)
                    send_to_help_buf(spaces, 20, FALSE);
                else
                    break;
            }
        }
    }
}

char* opt_help_message(void)
{
    if (help_buf == NULL)
        _generate_help_message();
    return help_buf;
}

void opt_register_pre_note(char* note)
{
    if (n_pre_notes == max_pre_notes)
        pre_notes = grow_option_array(pre_notes, &max_pre_notes, sizeof *pre_notes);
    pre_notes[n_pre_notes++] = note;
}

#define ERRMSG_STREAM stderr

void opt_register(char* new_interface, option_t* new_opts, int n_new_opts)
{
    int block_i;
    struct opt_block* block;

    if (help_buf) {
        /* internal error, must register all options before generating
	 help message */
        fprintf(ERRMSG_STREAM, "\
%s: internal options error: opt register after help message generation.\n",
                program_name);
        exit(-16);
    }

    /* check for conflicting options */
    for (block_i = 0; block_i < n_opt_blocks; block_i++) {
        char* interface = opt_blocks[block_i].interface;
        option_t* opts = opt_blocks[block_i].opts;
        int n_opts = opt_blocks[block_i].n_opts;
        int opt_i, new_opt_i;

        for (opt_i = 0; opt_i < n_opts; opt_i++)
            for (new_opt_i = 0; new_opt_i < n_new_opts; new_opt_i++) {
                if (!strcmp(opts[opt_i].text, new_opts[new_opt_i].text)) {
                    /* conflicting options */
                    fprintf(ERRMSG_STREAM, "\
%s: opt internal error: `%s' and `%s' both request option `%s'\n",
                            program_name, interface, new_interface, opts[opt_i].text);
                    exit(-16);
                }
            }
    }

    if (n_opt_blocks == n_opt_blocks_max)
        opt_blocks = grow_option_array(opt_blocks, &n_opt_blocks_max, sizeof *opt_blocks);

    block = &opt_blocks[n_opt_blocks++];

    block->interface = new_interface;
    block->opts = new_opts;
    block->n_opts = n_new_opts;
}

opt_database_t* opt_alloc_db(void)
{
    opt_database_t* retval;

    retval = option_malloc(sizeof *retval);

    retval->opt_vals = NULL;
    retval->n_opt_vals = 0;
    /* default maximum */
    retval->max_opt_vals = 0;

    return retval;
}

PRIVATE opt_val_t* opt_lookup_helper(opt_database_t* db, char* opt)
{
    opt_val_t* retval;
    int i;

    retval = 0;

    /* try to find this option in the database */
    for (i = 0; i < db->n_opt_vals; i++) {
        if (strcmp(db->opt_vals[i].text, opt) == 0) {
            retval = &db->opt_vals[i];
            break;
        }
    }
    return retval;
}

opt_val_t* opt_lookup(opt_database_t* db, char* opt)
{
    opt_val_t* retval;

    retval = opt_lookup_helper(db, opt);

    return retval;
}

void opt_put_val(opt_database_t* db, char* opt, char* val, priority_t pri, int temp_val_p)
{
    opt_val_t* opt_val;

    opt_val = opt_lookup_helper(db, opt);
    if (!opt_val) {
        if (db->n_opt_vals == db->max_opt_vals)
            db->opt_vals = grow_option_array(db->opt_vals, &db->max_opt_vals, sizeof *db->opt_vals);
        opt_val = &db->opt_vals[db->n_opt_vals++];

        opt_val->text = opt;
        opt_val->val = NULL;
        opt_val->t_val = NULL;
    }

    if (temp_val_p) {
        opt_val->t_val = val;
        opt_val->t_pri = pri;
    }
    else {
        opt_val->val = val;
        opt_val->pri = pri;
    }
}

void opt_put_int_val(opt_database_t* db, char* opt, int valint, priority_t pri, int temp_val_p)
{
    char *val, buf[256];

    sprintf(buf, "%d", valint);
    val = option_malloc(strlen(buf) + 1);
    strcpy(val, buf);

    opt_put_val(db, opt, val, pri, temp_val_p);
}

#define option_value(opt_val) ((opt_val)->t_val ?: (opt_val)->val)

int opt_val(opt_database_t* db, char* opt, char** retval)
{
    opt_val_t* opt_val;
    char* val = NULL;
    boolean_t found_p = FALSE;

    opt_val = opt_lookup(db, opt);
    if (opt_val) {
        val = option_value(opt_val);
        if (val) {
            if (retval)
                *retval = val;
            found_p = TRUE;
        }
    }
    return found_p;
}

/* Parses an integer value and returns it in *retval.  If
 * parse_error_p is non-NULL, sets parse_error_p to TRUE and prints an
 * error message in case of a parse error, else leaves it untouched.
 * Returns TRUE if a value was found.
 */
int opt_int_val(opt_database_t* db, char* opt, int* retval, boolean_t* parse_error_p)
{
    opt_val_t* opt_val;
    char* val = NULL;

    opt_val = opt_lookup(db, opt);
    if (opt_val && (val = option_value(opt_val)) && retval) {
        int32 v;
        if (!parse_number(val, &v, 1)) {
            if (parse_error_p) {
                fprintf(stderr, "Malformed numeric argument to -%s.\n", opt);
                *parse_error_p = TRUE;
            }
            return FALSE;
        }
        *retval = v;
    }

    /* Do *NOT* touch *parse_error_p if there is no error. */

    return opt_val && val;
}

int opt_parse(opt_database_t* db, option_t* opts, int n_opts, int* argc, char* argv[])
{
    int parse_error_p = FALSE;
    int i;
    int argc_left;

    /* we are sure to copy the null termination because we start with
     `i = 1' */
    argc_left = *argc;

    /* skip over the program name */
    for (i = 1; i < *argc; i++, argc_left--) {
        char* arg;
        int arg_i;

        arg_i = i;
        arg = argv[i];
        /* option */
        if (*arg == '-') {
            int opt_i;

            /* find the option among the options */
            for (opt_i = 0; opt_i < n_opts; opt_i++) {
                option_t* opt = &opts[opt_i];

                if (!strcmp(&arg[1], opt->text)) {
                    char* optval = NULL;

                    /* found the option */
                    switch (opt->kind) {
                    case opt_no_arg:
                        optval = "1";
                        break;
                    case opt_optional:
                        if ((i + 1) < *argc && *argv[i + 1] != '-') {
                            optval = argv[++i];
                            argc_left--;
                        }
                        else
                            optval = opt->opt_val;
                        break;
                    case opt_sticky:
                        optval = &arg[1 + strlen(opt->text)];
                        break;
                    case opt_sep:
                        if ((i + 1) < *argc) {
                            optval = argv[++i];
                            argc_left--;
                        }
                        else {
                            fprintf(ERRMSG_STREAM, "\
%s: option `-%s' requires argument\n",
                                    program_name, opt->text);
                            parse_error_p = TRUE;
                        }
                        break;
                    case opt_ignore:
                        goto next_arg;
                    case opt_sep_ignore:
                        if ((i + 1) < *argc) {
                            i++;
                            argc_left--;
                        }
                        else {
                            fprintf(ERRMSG_STREAM, "\
%s: option `-%s' requires argument\n",
                                    program_name, opt->text);
                            parse_error_p = TRUE;
                        }
                        goto next_arg;
                    }
                    *argc -= (i + 1) - arg_i;
                    memmove(&argv[arg_i], &argv[i + 1], (argc_left - 1) * sizeof *argv);
                    i = arg_i - 1;
                    opt_put_val(db, opt->text, optval, pri_command_line, FALSE);
                }
            }
        }
    next_arg:;
    }
    return parse_error_p;
}
