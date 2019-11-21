/*
Calf Box, an open source musical instrument.
Copyright (C) 2010-2011 Krzysztof Foltman

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "sfzparser.h"
#include "tarfile.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int debug_variable_definitions = 0;
int debug_variable_substitutions = 0;
int debug_includes = 0;

struct sfz_parser_state
{
    struct sfz_parser_client *client;
    gboolean (*handler)(struct sfz_parser_state *state, int ch);
    const char *filename;
    const char *buf;
    int pos, len, line;
    int token_start;
    int key_start, key_end;
    int value_start, value_end;
    GHashTable *variables;
    struct cbox_tarfile *tarfile;
    GError **error;
};

static gboolean load_sfz_into_state(struct sfz_parser_state *s, const char *name);
static gboolean handle_char(struct sfz_parser_state *state, int ch);

static void unexpected_char(struct sfz_parser_state *state, int ch)
{
    g_set_error(state->error, CBOX_SFZPARSER_ERROR, CBOX_SFZ_PARSER_ERROR_INVALID_CHAR, "Unexpected character '%c' (%d)", ch, ch);
}

static gboolean handle_header(struct sfz_parser_state *state, int ch)
{
    if (ch >= 'a' && ch <= 'z')
        return TRUE;
    if (ch == '>')
    {
        char *token = g_strndup(state->buf + state->token_start, state->pos - 1 - state->token_start);
        gboolean result = state->client->token(state->client, token, state->error);
        g_free(token);
        state->handler = handle_char;
        return result;
    }
    unexpected_char(state, ch);
    return FALSE;
}

static void scan_for_value(struct sfz_parser_state *state)
{
    state->value_start = state->pos;
    while(state->pos < state->len)
    {
        if (state->pos < state->len + 2 && state->buf[state->pos] == '/' && state->buf[state->pos + 1] == '/')
        {
            state->value_end = state->pos;
            while(state->value_end > state->value_start && isspace(state->buf[state->value_end - 1]))
                state->value_end--;
            state->pos += 2;
            while (state->pos < state->len && state->buf[state->pos] != '\r' && state->buf[state->pos] != '\n')
                state->pos++;
            return;
        }
        int ch = state->buf[state->pos];
        if (ch == 0 || ch == '\r' || ch == '\n' || ch == '<')
        {
            state->value_end = state->pos;
            // remove spaces before next key
            while(state->value_end > state->value_start && isspace(state->buf[state->value_end - 1]))
                state->value_end--;
            return;
        }
        if (ch == '=')
        {
            state->value_end = state->pos;
            // remove next key
            while(state->value_end > state->value_start && !isspace(state->buf[state->value_end - 1]))
                state->value_end--;
            // remove spaces before next key
            while(state->value_end > state->value_start && isspace(state->buf[state->value_end - 1]))
                state->value_end--;
            state->pos = state->value_end;
            return;
        }
        state->pos++;
    }
    state->value_end = state->pos;
    while(state->value_end > state->value_start && isspace(state->buf[state->value_end - 1]))
        state->value_end--;
}

static gchar *expand_variables(struct sfz_parser_state *state, gchar *text)
{
    gchar *pos = strchr(text, '$');
    // No variables, no changes.
    if (!pos)
        return text;

    GString *result = g_string_new_len(text, pos - text);
    pos++;
    // Start of the variable name
    gchar *start = pos;
    if (!*start)
        return text;
    pos = start + 1;
    while(1)
    {
        gchar ch = *pos;
        gpointer value;
        if (ch)
        {
            *pos = '\0';
            value = g_hash_table_lookup(state->variables, start);
            *pos = ch;
        }
        else
            value = g_hash_table_lookup(state->variables, start);
        if (value)
        {
            g_string_append(result, value);
            // pos = first char that is not part of the variable name

            if (!ch)
                break;
            start = strchr(pos, '$');
            if (!start)
            {
                // Remainder has no variable references, add and stop
                g_string_append(result, pos);
                break;
            }
            // Add everything up to the next variable reference
            if (start != pos)
                g_string_append_len(result, pos, start - pos);
            // Restart, variable name starts at the next character
            start++;
            if (!*start)
                break;
            pos = start + 1;
        }
        else
        {
            if (!ch)
            {
                // Might throw an error here, but just quote the var name verbatim instead for now
                g_string_append(result, "$");
                g_string_append(result, start);
                break;
            }
            pos++;
        }
    }
    // Replace with a substituted version
    gchar *substituted = g_string_free(result, FALSE);
    if (debug_variable_substitutions)
        printf("Substitute: '%s' -> '%s'\n", text, substituted);
    g_free(text);
    return substituted;
}

static gboolean handle_key(struct sfz_parser_state *state, int ch)
{
    if (isalpha(ch) || isdigit(ch) || ch == '_')
        return TRUE;
    if(ch == '=')
    {
        state->key_end = state->pos - 1;
        scan_for_value(state);

        gchar *key = g_strndup(state->buf + state->key_start, state->key_end - state->key_start);
        gchar *value = g_strndup(state->buf + state->value_start, state->value_end - state->value_start);
        key = expand_variables(state, key);
        value = expand_variables(state, value);
        gboolean result = state->client->key_value(state->client, key, value);
        g_free(key);
        g_free(value);
        state->handler = handle_char;
        return result;
    }
    unexpected_char(state, ch);
    return FALSE;
}

static gboolean do_include(struct sfz_parser_state *state, const char *name)
{
    if (debug_includes)
        printf("Include file: %s\n", name);
    gchar *dir = g_path_get_dirname(state->filename);
    gchar *combined = g_build_filename(dir, name, NULL);
    gboolean result = load_sfz_into_state(state, combined);
    g_free(combined);
    g_free(dir);
    if (debug_includes)
        printf("End include file: %s\n", name);
    return result;
}

static void do_define(struct sfz_parser_state *state, char *key, char *value)
{
    if (debug_variable_definitions)
        printf("Define: '%s' -> '%s'\n", key, value);
    g_hash_table_insert(state->variables, key, value);
}

static gboolean handle_include_filename(struct sfz_parser_state *state, int ch)
{
    if (ch == '"')
    {
        char *token = g_strndup(state->buf + state->token_start, state->pos - 1 - state->token_start);
        gboolean result = do_include(state, token);
        g_free(token);
        state->handler = handle_char;
        return result;
    }
    if ((unsigned)(ch) >= ' ')
        return TRUE;
    unexpected_char(state, ch);
    return FALSE;
}

static gboolean handle_include_skipwhite(struct sfz_parser_state *state, int ch)
{
    if (isspace(ch))
        return TRUE;
    if (ch == '"')
    {
        state->token_start = state->pos;
        state->handler = handle_include_filename;
        return TRUE;
    }
    unexpected_char(state, ch);
    return FALSE;
}

static gboolean handle_define_value(struct sfz_parser_state *state, int ch)
{
    if (ch == '\n' || ch == '\r' || ch == -1)
    {
        char *key = g_strndup(state->buf + state->key_start, state->key_end - state->key_start);
        char *value = g_strndup(state->buf + state->value_start, state->pos - state->value_start - 1);
        do_define(state, key, value);
        state->handler = handle_char;
        return TRUE;
    }
    return TRUE;
}

static gboolean handle_define_skipwhite2(struct sfz_parser_state *state, int ch)
{
    if (ch == '\n' || ch == '\r' || ch == -1)
    {
        char *key = g_strndup(state->buf + state->key_start, state->key_end - state->key_start);
        g_set_error(state->error, CBOX_SFZPARSER_ERROR, CBOX_SFZ_PARSER_ERROR_INVALID_CHAR, "Unspecified variable value for '%s'", key);
        g_free(key);
        return FALSE;
    }
    if (isspace(ch))
        return TRUE;
    state->value_start = state->pos - 1;
    state->handler = handle_define_value;
    return TRUE;
}

static gboolean handle_define_varname(struct sfz_parser_state *state, int ch)
{
    if (ch == '\n' || ch == '\r' || ch == -1)
    {
        char *key = g_strndup(state->buf + state->key_start, state->key_end - state->key_start);
        g_set_error(state->error, CBOX_SFZPARSER_ERROR, CBOX_SFZ_PARSER_ERROR_INVALID_CHAR, "Unspecified variable name");
        g_free(key);
        return FALSE;
    }
    if (isspace(ch))
    {
        state->key_end = state->pos - 1;
        state->handler = handle_define_skipwhite2;
        return TRUE;
    }
    if (ch >= 33 && ch <= 127 && ch != '$')
        return TRUE;
    unexpected_char(state, ch);
    return FALSE;
}

static gboolean handle_define_skipwhite(struct sfz_parser_state *state, int ch)
{
    if (isspace(ch))
        return TRUE;
    if (ch == '$')
    {
        state->key_start = state->pos;
        state->handler = handle_define_varname;
        return TRUE;
    }

    unexpected_char(state, ch);
    return FALSE;
}

static gboolean handle_preprocessor(struct sfz_parser_state *state, int ch)
{
    if (isalpha(ch))
        return TRUE;
    if (isspace(ch))
    {
        if (!memcmp(state->buf + state->token_start, "include", state->pos - state->token_start - 1))
        {
            state->handler = handle_include_skipwhite;
            return TRUE;
        }
        if (!memcmp(state->buf + state->token_start, "define", state->pos - state->token_start - 1))
        {
            state->handler = handle_define_skipwhite;
            return TRUE;
        }
        char *preproc = g_strndup(state->buf + state->token_start, state->pos - state->token_start - 1);
        g_set_error(state->error, CBOX_SFZPARSER_ERROR, CBOX_SFZ_PARSER_ERROR_INVALID_CHAR, "%s:%d Unsupported parser directive '%s'", state->filename, state->line, preproc);
        g_free(preproc);
        state->handler = handle_char;
        return FALSE;
    }
    unexpected_char(state, ch);
    return FALSE;
}

static gboolean handle_char(struct sfz_parser_state *state, int ch)
{
    if (isalpha(ch) || isdigit(ch))
    {
        state->key_start = state->pos - 1;
        state->handler = handle_key;
        return TRUE;
    }
    switch(ch)
    {
    case '_':
        return TRUE;

    case '\r':
    case '\n':
    case ' ':
    case '\t':
    case -1:
        return TRUE;
    case '<':
        state->token_start = state->pos;
        state->handler = handle_header;
        return TRUE;
    case '#':
        state->token_start = state->pos;
        state->handler = handle_preprocessor;
        return TRUE;
    default:
        unexpected_char(state, ch);
        return FALSE;
    }
}

gboolean load_sfz_from_string_into_state(struct sfz_parser_state *s, const char *buf, int len)
{
    const char *oldbuf = s->buf;
    int oldpos = s->pos, oldlen = s->len, oldline = s->line;
    gboolean ok = FALSE;
    s->buf = buf;
    s->pos = 0;
    s->len = len;
    s->handler = handle_char;
    s->token_start = 0;
    if (len >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF)
    {
        // UTF-8 BOM
        s->pos += 3;
    }
    while(s->pos < len && s->handler != NULL)
    {
        if (s->pos < len + 2 && buf[s->pos] == '/' && buf[s->pos + 1] == '/')
        {
            s->pos += 2;
            while (s->pos < len && buf[s->pos] != '\r' && buf[s->pos] != '\n')
                s->pos++;
            continue;
        }
        char ch = buf[s->pos++];
        gboolean newline = FALSE, eat_lf = FALSE;
        // Convert CR or CR/LF to LF
        if (ch == '\r') {
            newline = TRUE;
            eat_lf = (buf[s->pos] == '\n');
            ch = '\n';
        } else if (ch == '\n')
            newline = TRUE;

        if (!(*s->handler)(s, ch))
            goto restore;
        if (newline)
            s->line++;
        if (eat_lf)
            s->pos++;
    }
    if (s->handler)
    {
        if (!(*s->handler)(s, -1))
            goto restore;
    }
    ok = TRUE;
restore:
    s->buf = oldbuf;
    s->pos = oldpos;
    s->line = oldline;
    s->len = oldlen;
    s->handler = handle_char;
    s->token_start = oldpos;
    return ok;
}

gboolean load_sfz_from_string(const char *buf, int len, struct sfz_parser_client *c, GError **error)
{
    struct sfz_parser_state s;
    memset(&s, 0, sizeof(s));
    s.line = 1;
    s.filename = "<inline>";
    s.tarfile = NULL;
    s.client = c;
    s.error = error;
    s.variables = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    gboolean result = load_sfz_from_string_into_state(&s, buf, len);
    g_hash_table_destroy(s.variables);
    return result;
}

gboolean load_sfz_into_state(struct sfz_parser_state *s, const char *name)
{
    g_clear_error(s->error);
    FILE *f;
    int len = -1;
    if (s->tarfile)
    {
        struct cbox_taritem *item = cbox_tarfile_get_item_by_name(s->tarfile, name, TRUE);
        if (!item)
        {
            g_set_error(s->error, G_FILE_ERROR, g_file_error_from_errno (2), "Cannot find '%s' in the tarfile", name);
            return FALSE;
        }
        int fd = cbox_tarfile_openitem(s->tarfile, item);
        if (fd < 0)
        {
            g_set_error(s->error, G_FILE_ERROR, g_file_error_from_errno (errno), "Cannot open '%s' in the tarfile", name);
            return FALSE;
        }
        f = fdopen(fd, "rb");
        len = item->size;
    }
    else
        f = fopen(name, "rb");

    if (!f)
    {
        g_set_error(s->error, G_FILE_ERROR, g_file_error_from_errno (errno), "Cannot open '%s'", name);
        return FALSE;
    }
    
    if (len == -1)
    {
        fseek(f, 0, SEEK_END);
        len = ftell(f);
        fseek(f, 0, SEEK_SET);
    }
    
    unsigned char *buf = malloc(len + 1);
    buf[len] = '\0';
    if (fread(buf, 1, len, f) != (size_t)len)
    {
        g_set_error(s->error, G_FILE_ERROR, g_file_error_from_errno (errno), "Cannot read '%s'", name);
        fclose(f);
        return FALSE;
    }
    fclose(f);

    gboolean result = load_sfz_from_string_into_state(s, (char *)buf, len);
    free(buf);
    return result;
}

gboolean load_sfz(const char *name, struct cbox_tarfile *tarfile, struct sfz_parser_client *c, GError **error)
{
    struct sfz_parser_state s;
    memset(&s, 0, sizeof(s));
    s.line = 1;
    s.filename = name;
    s.tarfile = tarfile;
    s.client = c;
    s.error = error;
    s.variables = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    gboolean result = load_sfz_into_state(&s, name);
    g_hash_table_destroy(s.variables);
    return result;
}

GQuark cbox_sfz_parser_error_quark(void)
{
    return g_quark_from_string("cbox-sfz-parser-error-quark");
}
