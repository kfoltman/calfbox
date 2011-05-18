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
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct sfz_parser_state
{
    struct sfz_parser_client *client;
    void (*handler)(struct sfz_parser_state *state, int ch);
    const char *filename;
    const char *buf;
    int pos;
    int token_start;
    int key_start, key_end;
    int value_start, value_end;
    GError **error;
};

static void handle_char(struct sfz_parser_state *state, int ch);

static void handle_2ndslash(struct sfz_parser_state *state, int ch)
{
    if (ch == '\r' || ch == '\n')
    {
        state->handler = handle_char;
        state->token_start = state->pos;
    }
    // otherwise, consume
}

static void unexpected_char(struct sfz_parser_state *state, int ch)
{
    g_set_error(state->error, g_quark_from_string("sfzparser"), SFZ_ERR_INVALID_CHAR, "Unexpected character '%c'", ch);
}

static void handle_postslash(struct sfz_parser_state *state, int ch)
{
    if (ch == '/')
        state->handler = handle_2ndslash;
    else
        unexpected_char(state, ch);
}

static void handle_header(struct sfz_parser_state *state, int ch)
{
    if (ch >= 'a' && ch <= 'z')
        return;
    if (ch == '>')
    {
        if (!strncmp(state->buf + state->token_start, "region", state->pos - 1 - state->token_start))
        {
            state->client->region(state->client);
        }
        else
        if (!strncmp(state->buf + state->token_start, "group", state->pos - 1 - state->token_start))
        {
            state->client->group(state->client);
        }
        else
        {
            gchar *tmp = g_strndup(state->buf + state->token_start, state->pos - 1 - state->token_start);
            g_set_error(state->error, g_quark_from_string("sfzparser"), SFZ_ERR_INVALID_HEADER, "Unexpected header <%s>", tmp);
            g_free(tmp);
            return;
        }
        state->handler = handle_char;
        return;
    }
    unexpected_char(state, ch);
}

static void scan_for_value(struct sfz_parser_state *state)
{
    state->value_start = state->pos;
    while(1)
    {
        int ch = state->buf[state->pos];
        if (ch == 0 || ch == '\r' || ch == '\n')
        {
            state->value_end = state->pos;
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
}

static void handle_key(struct sfz_parser_state *state, int ch)
{
    if (isalpha(ch) || isdigit(ch) || ch == '_')
        return;
    if(ch == '=')
    {
        state->key_end = state->pos - 1;
        scan_for_value(state);
        
        gchar *key = g_strndup(state->buf + state->key_start, state->key_end - state->key_start);
        gchar *value = g_strndup(state->buf + state->value_start, state->value_end - state->value_start);
        state->client->key_value(state->client, key, value);
        g_free(key);
        g_free(value);
        state->handler = handle_char;
        return;
    }
    unexpected_char(state, ch);
}

static void handle_char(struct sfz_parser_state *state, int ch)
{
    if (isalpha(ch) || isdigit(ch))
    {
        state->key_start = state->pos - 1;
        state->handler = handle_key;
        return;
    }
    switch(ch)
    {
    case '_':
        return;
        
    case '\r':
    case '\n':
    case ' ':
    case '\t':
    case -1:
        break;
    case '/':
        state->handler = handle_postslash;
        return;
    case '<':
        state->token_start = state->pos;
        state->handler = handle_header;
        return;
    default:
        g_set_error(state->error, g_quark_from_string("sfzparser"), SFZ_ERR_INVALID_CHAR, "Unexpected character '%c'", ch);
        return;
    }
}

void load_sfz(const char *name, struct sfz_parser_client *c, GError **error)
{
    g_clear_error(error);
    FILE *f = fopen(name, "rb");
    if (!f)
    {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno (errno), "Cannot open '%s'", name);
        return;
    }
    
    fseek(f, 0, 2);
    int len = ftell(f);
    fseek(f, 0, 0);
    
    char *buf = malloc(len + 1);
    buf[len] = '\0';
    if (fread(buf, 1, len, f) != len)
    {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno (errno), "Cannot read '%s'", name);
        fclose(f);
        return;
    }
    fclose(f);
    
    struct sfz_parser_state s;
    s.filename = name;
    s.buf = buf;
    s.handler = handle_char;
    s.pos = 0;
    s.token_start = 0;
    s.client = c;
    s.error = error;
    while(s.pos < len && s.handler != NULL)
    {
        (*s.handler)(&s, buf[s.pos++]);
        if (error && *error)
            return;
    }
    if (s.handler)
        (*s.handler)(&s, -1);
    
    free(buf);
}

#ifdef SFZPARSERTEST

void do_region(struct sfz_parser_client *client)
{
    printf("-- region\n");
}

void do_group(struct sfz_parser_client *client)
{
    printf("-- group\n");
}

void do_key_value(struct sfz_parser_client *client, const char *key, const char *value)
{
    printf("%s=%s\n", key, value);
}

int main(int argc, char *argv[])
{
    struct sfz_parser_client cli;
    cli.user_data = NULL;
    cli.region = do_region;
    cli.group = do_group;
    cli.key_value = do_key_value;
    
    //load_sfz("horn.sfz", &cli);
    load_sfz(argv[1], &cli);
}

#endif
