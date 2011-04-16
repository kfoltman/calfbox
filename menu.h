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

#ifndef CBOX_MENU_H
#define CBOX_MENU_H

#include <ncurses.h>
#include <stdint.h>

enum cbox_menu_item_type
{
    menu_item_command,
    menu_item_value_int,
    menu_item_value_double,
    menu_item_value_enum,
    menu_item_submenu
};

struct cbox_menu_item;

struct cbox_menu_item_extras_command
{
    int (*execute)(struct cbox_menu_item *item, void *context);
};

struct cbox_menu_item_extras_int
{
    int vmin, vmax;
    const char *fmt;
};

struct cbox_menu_item_extras_double
{
    double vmin, vmax;
    const char *fmt;
    double (*step)(struct cbox_menu_item_extras_double *item, double value, int where);
    double step_arg;
};

struct cbox_menu_item
{
    const char *label;
    enum cbox_menu_item_type type;
    void *extras;
    void *value;
    int (*on_change)(struct cbox_menu_item *item, void *context);
    /* TODO: is_active? */
};

struct cbox_menu
{
    struct cbox_menu_item *items;
    int item_count;
    
    void (*destroy)(struct cbox_menu *);
};

#define FIXED_MENU(name) \
    struct cbox_menu menu_##name = { menu_items_##name, sizeof(menu_items_##name) / sizeof(struct cbox_menu_item), NULL };

struct cbox_menu_state;

extern struct cbox_menu_state *cbox_menu_state_new(struct cbox_menu *menu, WINDOW *window, void *context);
extern struct cbox_ui_page *cbox_menu_state_get_page(struct cbox_menu_state *st);
extern void cbox_menu_state_destroy(struct cbox_menu_state *st);

#endif
