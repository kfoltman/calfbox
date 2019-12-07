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

#if USE_NCURSES

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
    return item;
}

void cbox_menu_destroy(struct cbox_menu *menu)
{
    guint i;
    
    for (i = 0; i < menu->items->len; i++)
        cbox_menu_item_destroy(g_ptr_array_index(menu->items, i));
    
    g_ptr_array_free(menu->items, TRUE);
    g_string_chunk_free(menu->strings);
    free(menu);
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
    guint i;
    menu_state->size.label_width = 0;
    menu_state->size.value_width = 0;
    menu_state->size.height = 0;
    menu_state->yspace = getmaxy(menu_state->window) - 2;
    
    for (i = 0; i < menu->items->len; i++)
    {
        struct cbox_menu_item *item = g_ptr_array_index(menu->items, i);
        
        item->x = 1;
        item->y = 1 + menu_state->size.height;
        item->item_class->measure(item, menu_state);
    }
}

void cbox_menu_state_draw(struct cbox_menu_state *menu_state)
{
    struct cbox_menu *menu = menu_state->menu;
    guint i;
    
    werase(menu_state->window);
    box(menu_state->window, 0, 0);
    for (i = 0; i < menu->items->len; i++)
    {
        struct cbox_menu_item *item = g_ptr_array_index(menu->items, i);
        gchar *str = item->item_class->format_value(item, menu_state);
        item->item_class->draw(item, menu_state, str, menu_state->cursor == i);
        g_free(str);
    }
    wrefresh(menu_state->window);
}

static void cbox_menu_page_draw(struct cbox_ui_page *p)
{
    struct cbox_menu_page *mp = p->user_data;
    struct cbox_menu_state *st = mp->state;
    cbox_menu_state_size(st);
    cbox_menu_state_draw(st);
}

static int cbox_menu_is_item_enabled(struct cbox_menu *menu, unsigned int item)
{
    assert(item < menu->items->len);
    
    return ((struct cbox_menu_item *)g_ptr_array_index(menu->items, item))->item_class->on_key != NULL;
}

struct cbox_menu_state *cbox_menu_state_new(struct cbox_menu_page *page, struct cbox_menu *menu, WINDOW *window, void *context)
{
    struct cbox_menu_state *st = malloc(sizeof(struct cbox_menu_state));
    st->page = page;
    st->menu = menu;
    st->cursor = 0;
    st->yoffset = 0;
    st->window = window;
    st->context = context;
    st->caller = NULL;
    st->menu_is_temporary = 0;
    
    while(st->cursor < menu->items->len - 1 && !cbox_menu_is_item_enabled(menu, st->cursor))
        st->cursor++;
    
    return st;
}

int cbox_menu_page_on_idle(struct cbox_ui_page *p)
{
    struct cbox_menu_page *mp = p->user_data;
    struct cbox_menu_state *st = mp->state;
    cbox_menu_state_size(st);
    cbox_menu_state_draw(st);
    return 0;
}

int cbox_menu_page_on_key(struct cbox_ui_page *p, int ch)
{
    struct cbox_menu_page *mp = p->user_data;
    struct cbox_menu_state *st = mp->state;
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
        st = mp->state;
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
    case 12:
        wclear(st->window);
        return 0;
    case 27:
        return ch;
    case KEY_UP:
    case KEY_END:
        pos = ch == KEY_END ? menu->items->len - 1 : st->cursor - 1;
        while(pos >= 0 && !cbox_menu_is_item_enabled(menu, pos))
            pos--;
        if (pos >= 0)
            st->cursor = pos;
        if (ch == KEY_END)
        {
            st->yoffset = st->size.height - st->yspace;
            if (st->yoffset < 0)
                st->yoffset = 0;
        }
        else
        if (pos >= 0 && (guint)pos < menu->items->len)
        {
            int npos = st->cursor;
            int count = 0;
            // show up to 2 disabled items above
            while(npos >= 1 && !cbox_menu_is_item_enabled(menu, npos - 1) && count < 2)
            {
                npos--;
                count++;
            }
            item = g_ptr_array_index(menu->items, npos);
            if (item->y < 1 + st->yoffset)
                st->yoffset = item->y - 1;
        }
        cbox_menu_state_draw(st);
        return 0;
    case KEY_HOME:
    case KEY_DOWN:
        pos = ch == KEY_HOME ? 0 : st->cursor + 1;
        while(pos < (int)menu->items->len && !cbox_menu_is_item_enabled(menu, pos))
            pos++;
        if (pos < (int)menu->items->len)
            st->cursor = pos;
        if (ch == KEY_HOME)
            st->yoffset = 0;
        else if (pos >= 0 && pos < (int)menu->items->len)
        {
            item = g_ptr_array_index(menu->items, st->cursor);
            if (item->y - 1 - st->yoffset >= st->yspace)
                st->yoffset = item->y - st->yspace;
        }
        cbox_menu_state_draw(st);
        return 0;
    }
    return 0;
}

void cbox_menu_state_destroy(struct cbox_menu_state *st)
{
    free(st);
}

struct cbox_menu_page *cbox_menu_page_new()
{
    struct cbox_menu_page *page = malloc(sizeof(struct cbox_menu_page));
    page->state = NULL;
    page->page.user_data = page;
    page->page.draw = cbox_menu_page_draw;
    page->page.on_key = cbox_menu_page_on_key;
    page->page.on_idle = cbox_menu_page_on_idle;
    return page;
}

void cbox_menu_page_destroy(struct cbox_menu_page *p)
{
    free(p);
}

#endif
