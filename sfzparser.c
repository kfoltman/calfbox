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

struct sfz_parser_state
{
    struct sfz_parser_client *client;
    gboolean (*handler)(struct sfz_parser_state *state, int ch);
    const char *filename;
    const char *buf;
    int pos, len;
    int token_start;
    int key_start, key_end;
    int value_start, value_end;
    GError **error;
};

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
        gboolean result = state->client->key_value(state->client, key, value);
        g_free(key);
        g_free(value);
        state->handler = handle_char;
        return result;
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
    default:
        unexpected_char(state, ch);
        return FALSE;
    }
}

gboolean load_sfz(const char *name, struct cbox_tarfile *tarfile, struct sfz_parser_client *c, GError **error)
{
    g_clear_error(error);
    FILE *f;
    int len = -1;
    if (tarfile)
    {
        struct cbox_taritem *item = cbox_tarfile_get_item_by_name(tarfile, name, TRUE);
        if (!item)
        {
            g_set_error(error, G_FILE_ERROR, g_file_error_from_errno (2), "Cannot find '%s' in the tarfile", name);
            return FALSE;
        }
        int fd = cbox_tarfile_openitem(tarfile, item);
        if (fd < 0)
        {
            g_set_error(error, G_FILE_ERROR, g_file_error_from_errno (errno), "Cannot open '%s' in the tarfile", name);
            return FALSE;
        }
        f = fdopen(fd, "rb");
        len = item->size;
    }
    else
        f = fopen(name, "rb");

    if (!f)
    {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno (errno), "Cannot open '%s'", name);
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
    if (fread(buf, 1, len, f) != len)
    {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno (errno), "Cannot read '%s'", name);
        fclose(f);
        return FALSE;
    }
    fclose(f);
    gboolean result = load_sfz_from_string((char *)buf, len, c, error);
    free(buf);
    return result;
}

gboolean load_sfz_from_string(const char *buf, int len, struct sfz_parser_client *c, GError **error)
{
    struct sfz_parser_state s;
    s.filename = "<inline>";
    s.buf = buf;
    s.handler = handle_char;
    s.pos = 0;
    s.len = len;
    s.token_start = 0;
    s.client = c;
    s.error = error;
    if (len >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF)
    {
        // UTF-8 BOM
        s.pos += 3;
    }
    while(s.pos < len && s.handler != NULL)
    {
        if (s.pos < len + 2 && buf[s.pos] == '/' && buf[s.pos + 1] == '/')
        {
            s.pos += 2;
            while (s.pos < len && buf[s.pos] != '\r' && buf[s.pos] != '\n')
                s.pos++;
            continue;
        }
        if (!(*s.handler)(&s, buf[s.pos++]))
            return FALSE;
    }
    if (s.handler)
    {
        if (!(*s.handler)(&s, -1))
            return FALSE;
    }
    
    return TRUE;
}

GQuark cbox_sfz_parser_error_quark(void)
{
    return g_quark_from_string("cbox-sfz-parser-error-quark");
}
