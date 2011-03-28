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

#include <glib.h>
#include <ncurses.h>
#include <string.h>

struct cbox_menu_state
{
    struct cbox_menu *menu;
    int cursor;
    int label_width, value_width;
    WINDOW *window;
};

void cbox_ui_start()
{
    initscr();
    cbreak();
    noecho();
    start_color();    
    keypad(stdscr, TRUE);
}

void cbox_ui_stop()
{
    endwin();    
}

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

void cbox_ui_size_menu(struct cbox_menu_state *menu_state)
{
    struct cbox_menu *menu = menu_state->menu;
    int label_width = 0;
    int value_width = 0;
    int i;
    
    for (i = 0; i < menu->item_count; i++)
    {
        const struct cbox_menu_item *item = menu->items + i;
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

void cbox_ui_draw_menu(struct cbox_menu_state *menu_state)
{
    struct cbox_menu *menu = menu_state->menu;
    int i;
    
    box(menu_state->window, 0, 0);
    for (i = 0; i < menu->item_count; i++)
    {
        const struct cbox_menu_item *item = menu->items + i;
        gchar *str = cbox_menu_item_value_format(item);
        if (menu_state->cursor == i)
            wattron(menu_state->window, A_REVERSE);
        mvwprintw(menu_state->window, i + 1, 1, "%-*s %*s", menu_state->label_width, item->label, menu_state->value_width, str);
        wattroff(menu_state->window, A_REVERSE);
        g_free(str);
    }
    wrefresh(menu_state->window);
}

int cbox_ui_run_menu(struct cbox_menu *menu, void *context)
{
    struct cbox_menu_state st = { .menu = menu, .cursor = 0, .window = stdscr };
    int ch;
    
    cbox_ui_size_menu(&st);
    cbox_ui_draw_menu(&st);
    while(1)
    {
        struct cbox_menu_item *item = NULL;
        if (st.cursor >= 0 && st.cursor < menu->item_count)
            item = &menu->items[st.cursor];
            
        ch = getch();
        if (ch == 10)
        {
            if (item)
            {
                int exit_menu = 0;
                if (item->type == menu_item_command)
                {
                    struct cbox_menu_item_extras_command *cmd = item->extras;
                    if (cmd && cmd->execute)
                        exit_menu = cmd->execute(item, context);
                }
                if (item->on_change)
                    exit_menu = item->on_change(item, context) || exit_menu;
                
                if (exit_menu)
                    return exit_menu;
            }
            continue;
        }
        if (ch == 27)
            break;
        switch(ch)
        {
        case KEY_LEFT:
            if (!item || !item->extras || !item->value)
                continue;
            if (item->type == menu_item_value_int)
            {
                struct cbox_menu_item_extras_int *ix = item->extras;
                int *pv = item->value;
                if (*pv > ix->vmin)
                {
                    (*pv)--;
                    if (item->on_change)
                        item->on_change(item, context);
                    cbox_ui_size_menu(&st);
                    cbox_ui_draw_menu(&st);
                }
            }
            continue;
        case KEY_RIGHT:
            if (!item || !item->extras || !item->value)
                continue;
            if (item->type == menu_item_value_int)
            {
                struct cbox_menu_item_extras_int *ix = item->extras;
                int *pv = item->value;
                if (*pv < ix->vmax)
                {
                    (*pv)++;
                    if (item->on_change)
                        item->on_change(item, context);
                    cbox_ui_size_menu(&st);
                    cbox_ui_draw_menu(&st);
                }
            }
            continue;
            
        case KEY_UP:
            if (st.cursor > 0)
                st.cursor--;
            cbox_ui_draw_menu(&st);
            continue;
        case KEY_DOWN:
            if (st.cursor < menu->item_count - 1)
                st.cursor++;
            cbox_ui_draw_menu(&st);
            continue;
        }
    }
    return 27;
}

