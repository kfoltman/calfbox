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

#ifndef CBOX_MENUITEM_H
#define CBOX_MENUITEM_H

#include <glib.h>
#include <ncurses.h>
#include <stdint.h>

struct cbox_menu;
struct cbox_menu_item;
struct cbox_menu_item_command;
struct cbox_menu_item_static;
struct cbox_menu_state;

struct cbox_menu_measure
{
    int label_width;
    int value_width;
    int height;
};

struct cbox_menu_item_class
{
    void (*measure)(struct cbox_menu_item *item, struct cbox_menu_state *state);
    void (*draw)(struct cbox_menu_item *item, struct cbox_menu_state *state, int *y, int *x, gchar *value, int hilited);
    gchar *(*format_value)(const struct cbox_menu_item *item, struct cbox_menu_state *state);
    int (*on_key)(struct cbox_menu_item *item, struct cbox_menu_state *state, int key);
    int (*on_idle)(struct cbox_menu_item *item, struct cbox_menu_state *state);
    void (*destroy)(struct cbox_menu_item *item);
    
};

typedef int (*cbox_menu_item_execute_func)(struct cbox_menu_item_command *item, void *context);
typedef char *(*cbox_menu_item_format_value)(const struct cbox_menu_item_static *item, void *context);

struct cbox_menu_item
{
    gchar *label;
    struct cbox_menu_item_class *item_class;
    void *item_context;
    /* TODO: is_active? */
    /* TODO: x/y */
};

struct cbox_menu_item_command
{
    struct cbox_menu_item item;
    int (*execute)(struct cbox_menu_item_command *item, void *menu_context);
};

struct cbox_menu_item_int
{
    struct cbox_menu_item item;
    int *value;
    int vmin, vmax;
    const char *fmt;
    int (*on_change)(struct cbox_menu_item_int *item, void *menu_context);
};

struct cbox_menu_item_double
{
    struct cbox_menu_item item;
    double *value;
    double vmin, vmax;
    const char *fmt;
    double step_arg;
    double (*step)(struct cbox_menu_item_double *item, int where);
    int (*on_change)(struct cbox_menu_item_double *item, void *menu_context);
};

struct cbox_menu_item_static
{
    struct cbox_menu_item item;
    char *(*format_value)(const struct cbox_menu_item_static *item, void *menu_context);
};

extern struct cbox_menu_item *cbox_menu_item_new_command(const char *label, cbox_menu_item_execute_func exec, void *item_context);
extern struct cbox_menu_item *cbox_menu_item_new_static(const char *label, cbox_menu_item_format_value fmt, void *item_context);
extern struct cbox_menu_item *cbox_menu_item_new_int(const char *label, int *value, int vmin, int vmax, void *item_context);
extern void cbox_menu_item_destroy(struct cbox_menu_item *);

#endif
