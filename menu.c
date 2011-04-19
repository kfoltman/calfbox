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
#include "menuitem.h"
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

static int cbox_menu_page_on_key(struct cbox_ui_page *p, int ch);
static int cbox_menu_page_on_idle(struct cbox_ui_page *p);

struct cbox_menu *cbox_menu_new()
{
    struct cbox_menu *menu = malloc(sizeof(struct cbox_menu));
    
    menu->items = g_ptr_array_new();
    menu->strings = g_string_chunk_new(100);
    return menu;
}

struct cbox_menu_item *cbox_menu_add_item(struct cbox_menu *menu, struct cbox_menu_item *item)
{    
    g_ptr_array_add(menu->items, item);
}

void cbox_menu_destroy(struct cbox_menu *menu)
{
    int i;
    
    for (i = 0; i < menu->items->len; i++)
        cbox_menu_item_destroy(g_ptr_array_index(menu->items, i));
    
    g_ptr_array_free(menu->items, TRUE);
    g_string_chunk_free(menu->strings);
}


/*
gchar *cbox_menu_item_value_format(const struct cbox_menu_item *item, void *context)
{
    switch(item->type)
    {
        case menu_item_static:
            if (item->extras)
                return ((struct cbox_menu_item_extras_static *)(item->extras))->format_value(item, context);
            else
                return g_strdup_printf("");
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
*/

void cbox_menu_state_size(struct cbox_menu_state *menu_state)
{
    struct cbox_menu *menu = menu_state->menu;
    int label_width = 0;
    int value_width = 0;
    int i;
    
    for (i = 0; i < menu->items->len; i++)
    {
        const struct cbox_menu_item *item = g_ptr_array_index(menu->items, i);
        gchar *value = item->item_class->format_value(item, menu_state);
        
        int len = strlen(item->label);
        int len2 = value ? strlen(value) : 0;
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
    int i, x, y;
    
    wclear(menu_state->window);
    box(menu_state->window, 0, 0);
    x = 1;
    y = 1;
    for (i = 0; i < menu->items->len; i++)
    {
        struct cbox_menu_item *item = g_ptr_array_index(menu->items, i);
        gchar *str = item->item_class->format_value(item, menu_state);
        if (item->item_class->draw)
        {
            item->item_class->draw(item, menu_state, &y, &x, str, menu_state->cursor == i);
        }
        else
        {
            if (menu_state->cursor == i)
                wattron(menu_state->window, A_REVERSE);
            mvwprintw(menu_state->window, y, x, "%-*s %*s", menu_state->label_width, item->label, menu_state->value_width, str);
            wattroff(menu_state->window, A_REVERSE);
            y++;
        }
        g_free(str);
    }
    wrefresh(menu_state->window);
}

static void cbox_menu_page_draw(struct cbox_ui_page *p)
{
    struct cbox_menu_state *st = p->user_data;
    cbox_menu_state_size(st);
    cbox_menu_state_draw(st);
}

static int cbox_menu_is_item_enabled(struct cbox_menu *menu, int item)
{
    assert(item >= 0 && item < menu->items->len);
    
    return ((struct cbox_menu_item *)g_ptr_array_index(menu->items, item))->item_class->on_key != NULL;
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
    st->page.on_idle = cbox_menu_page_on_idle;
    
    while(st->cursor < menu->items->len - 1 && !cbox_menu_is_item_enabled(menu, st->cursor))
        st->cursor++;
    
    return st;
}

struct cbox_ui_page *cbox_menu_state_get_page(struct cbox_menu_state *state)
{
    return &state->page;
}

int cbox_menu_page_on_idle(struct cbox_ui_page *p)
{
    struct cbox_menu_state *st = p->user_data;
    cbox_menu_state_size(st);
    cbox_menu_state_draw(st);
    return 0;
}

int cbox_menu_page_on_key(struct cbox_ui_page *p, int ch)
{
    struct cbox_menu_state *st = p->user_data;
    struct cbox_menu *menu = st->menu;
    struct cbox_menu_item *item = NULL;
    int pos = st->cursor;
    int res = 0;
    if (st->cursor >= 0 && st->cursor < menu->items->len)
        item = g_ptr_array_index(menu->items, st->cursor);
        
    if (ch == 27)
        return ch;

    if (item->item_class->on_key)
    {
        res = item->item_class->on_key(item, st, ch);
        if (res < 0)
        {
            cbox_menu_state_size(st);
            cbox_menu_state_draw(st);
            return 0;
        }
    }

    if (res > 0)
        return res;
    
    switch(ch)
    {
    case 27:
        return ch;
    case KEY_UP:
    case KEY_END:
        pos = ch == KEY_END ? menu->items->len - 1 : st->cursor - 1;
        while(pos >= 0 && !cbox_menu_is_item_enabled(menu, pos))
            pos--;
        if (pos >= 0)
        {
            st->cursor = pos;
            cbox_menu_state_draw(st);
        }
        return 0;
    case KEY_HOME:
    case KEY_DOWN:
        pos = ch == KEY_HOME ? 0 : st->cursor + 1;
        while(pos < menu->items->len && !cbox_menu_is_item_enabled(menu, pos))
            pos++;
        if (pos < menu->items->len)
        {
            st->cursor = pos;
            cbox_menu_state_draw(st);
        }
        cbox_menu_state_draw(st);
        return 0;
    }
    return 0;
}

extern void cbox_menu_state_destroy(struct cbox_menu_state *st)
{
    free(st);
}
