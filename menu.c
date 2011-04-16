/*
Calf Box, an open source musical instrument.
Copyright (C) 2010 Krzysztof Foltman

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

#include "menu.h"
#include "ui.h"

#include <assert.h>
#include <glib.h>
#include <malloc.h>
#include <ncurses.h>
#include <string.h>

struct cbox_menu
{
    GPtrArray *items;
    GStringChunk *strings;
};

struct cbox_menu *cbox_menu_new()
{
    struct cbox_menu *menu = malloc(sizeof(struct cbox_menu));
    
    menu->items = g_ptr_array_new();
    menu->strings = g_string_chunk_new(100);
    return menu;
}

struct cbox_menu_item *cbox_menu_add_item(struct cbox_menu *menu, const char *label, enum cbox_menu_item_type type, void *extras, void *value)
{
    struct cbox_menu_item *item = malloc(sizeof(struct cbox_menu_item));
    item->label = g_string_chunk_insert(menu->strings, label);
    item->type = type;
    item->extras = extras;
    item->value = value;
    item->on_change = NULL;
    
    g_ptr_array_add(menu->items, item);
}

void cbox_menu_destroy(struct cbox_menu *menu)
{
    // XXXKF free individual items
    
    g_ptr_array_free(menu->items, TRUE);
    g_string_chunk_free(menu->strings);
}


struct cbox_menu_state
{
    struct cbox_ui_page page;
    struct cbox_menu *menu;
    int cursor;
    int label_width, value_width;
    WINDOW *window;
    void *context;
};

gchar *cbox_menu_item_value_format(const struct cbox_menu_item *item)
{
    switch(item->type)
    {
        case menu_item_submenu:
            return g_strdup_printf("...");
        case menu_item_command:
            return g_strdup_printf("<cmd>");
        default:
            if (!item->value)
                return g_strdup_printf("(null)");
            switch(item->type)
            {
                case menu_item_value_int:
                    return g_strdup_printf(((struct cbox_menu_item_extras_int *)(item->extras))->fmt, *(int *)item->value);
                case menu_item_value_double:
                    return g_strdup_printf(((struct cbox_menu_item_extras_double *)(item->extras))->fmt, *(double *)item->value);
                case menu_item_value_enum:
                    return g_strdup_printf("<enum>%d", *(int *)item->value);
                default:
                    return g_strdup_printf("");
            }
    }
    assert(0);
    return NULL;
}

void cbox_menu_state_size(struct cbox_menu_state *menu_state)
{
    struct cbox_menu *menu = menu_state->menu;
    int label_width = 0;
    int value_width = 0;
    int i;
    
    for (i = 0; i < menu->items->len; i++)
    {
        const struct cbox_menu_item *item = g_ptr_array_index(menu->items, i);
        gchar *value = cbox_menu_item_value_format(item);
        
        int len = strlen(item->label);
        int len2 = strlen(value);
        if (len > label_width)
            label_width = len;
        if (len2 > value_width)
            value_width = len2;
        
        g_free(value);
    }
    menu_state->label_width = label_width;
    menu_state->value_width = value_width;
}

void cbox_menu_state_draw(struct cbox_menu_state *menu_state)
{
    struct cbox_menu *menu = menu_state->menu;
    int i;
    
    box(menu_state->window, 0, 0);
    for (i = 0; i < menu->items->len; i++)
    {
        const struct cbox_menu_item *item = g_ptr_array_index(menu->items, i);
        gchar *str = cbox_menu_item_value_format(item);
        if (menu_state->cursor == i)
            wattron(menu_state->window, A_REVERSE);
        mvwprintw(menu_state->window, i + 1, 1, "%-*s %*s", menu_state->label_width, item->label, menu_state->value_width, str);
        wattroff(menu_state->window, A_REVERSE);
        g_free(str);
    }
    wrefresh(menu_state->window);
}

static int cbox_menu_page_on_key(struct cbox_ui_page *p, int ch);

static void cbox_menu_page_draw(struct cbox_ui_page *p)
{
    struct cbox_menu_state *st = p->user_data;
    cbox_menu_state_size(st);
    cbox_menu_state_draw(st);
}

struct cbox_menu_state *cbox_menu_state_new(struct cbox_menu *menu, WINDOW *window, void *context)
{
    struct cbox_menu_state *st = malloc(sizeof(struct cbox_menu_state));
    st->menu = menu;
    st->cursor = 0;
    st->window = window;
    st->context = context;
    st->page.user_data = st;
    st->page.draw = cbox_menu_page_draw;
    st->page.on_key = cbox_menu_page_on_key;
    st->page.on_idle = NULL;
    
    return st;
}

struct cbox_ui_page *cbox_menu_state_get_page(struct cbox_menu_state *state)
{
    return &state->page;
}

int cbox_menu_page_on_key(struct cbox_ui_page *p, int ch)
{
    struct cbox_menu_state *st = p->user_data;
    struct cbox_menu *menu = st->menu;
    struct cbox_menu_item *item = NULL;
    if (st->cursor >= 0 && st->cursor < menu->items->len)
        item = g_ptr_array_index(menu->items, st->cursor);
        
    if (ch == 10)
    {
        if (item)
        {
            int exit_menu = 0;
            if (item->type == menu_item_command)
            {
                struct cbox_menu_item_extras_command *cmd = item->extras;
                if (cmd && cmd->execute)
                    exit_menu = cmd->execute(item, st->context);
            }
            if (item->on_change)
                exit_menu = item->on_change(item, st->context) || exit_menu;
            
            if (exit_menu)
                return exit_menu;
        }
        return 0;
    }
    switch(ch)
    {
    case 27:
        return ch;
    case KEY_LEFT:
        if (!item || !item->extras || !item->value)
            return 0;
        if (item->type == menu_item_value_int)
        {
            struct cbox_menu_item_extras_int *ix = item->extras;
            int *pv = item->value;
            if (*pv > ix->vmin)
            {
                (*pv)--;
                if (item->on_change)
                    item->on_change(item, st->context);
                cbox_menu_state_size(st);
                cbox_menu_state_draw(st);
            }
        }
        return 0;
    case KEY_RIGHT:
        if (!item || !item->extras || !item->value)
            return 0;
        if (item->type == menu_item_value_int)
        {
            struct cbox_menu_item_extras_int *ix = item->extras;
            int *pv = item->value;
            if (*pv < ix->vmax)
            {
                (*pv)++;
                if (item->on_change)
                    item->on_change(item, st->context);
                cbox_menu_state_size(st);
                cbox_menu_state_draw(st);
            }
        }
        return 0;
        
    case KEY_UP:
        if (st->cursor > 0)
            st->cursor--;
        cbox_menu_state_draw(st);
        return 0;
    case KEY_DOWN:
        if (st->cursor < menu->items->len - 1)
            st->cursor++;
        cbox_menu_state_draw(st);
        return 0;
    }
}

extern void cbox_menu_state_destroy(struct cbox_menu_state *st)
{
    free(st);
}
