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

#if USE_NCURSES

#include <glib.h>
#include <ncurses.h>
#include <stdint.h>

struct cbox_menu;
struct cbox_menu_item;
struct cbox_menu_item_command;
struct cbox_menu_item_static;
struct cbox_menu_item_menu;
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
    void (*draw)(struct cbox_menu_item *item, struct cbox_menu_state *state, gchar *value, int hilited);
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
    uint32_t flags;
    int x, y;
    /* TODO: is_active? */
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

typedef struct cbox_menu *(*create_menu_func)(struct cbox_menu_item_menu *item, void *menu_context);

struct cbox_menu_item_context
{
    void (*destroy_func)(void *menu_context);
};

struct cbox_menu_item_menu
{
    struct cbox_menu_item item;
    struct cbox_menu *menu;
    create_menu_func create_menu;
};

enum {
    mif_free_label = 1, // release the label on destroy
    mif_free_context = 2, // release the context on destroy
    mif_dup_label = 4 | mif_free_label, // clone the label, release the clone on destroy
    mif_context_is_struct = 8, // cast context to cbox_menu_item_context and call destroy_func on destroy (it may or may not free() itself)
};

extern struct cbox_menu_item *cbox_menu_item_new_command(const char *label, cbox_menu_item_execute_func exec, void *item_context, uint32_t flags);
extern struct cbox_menu_item *cbox_menu_item_new_static(const char *label, cbox_menu_item_format_value fmt, void *item_context, uint32_t flags);
extern struct cbox_menu_item *cbox_menu_item_new_int(const char *label, int *value, int vmin, int vmax, void *item_context, uint32_t flags);
extern struct cbox_menu_item *cbox_menu_item_new_menu(const char *label, struct cbox_menu *menu, void *item_context, uint32_t flags);
extern struct cbox_menu_item *cbox_menu_item_new_dynamic_menu(const char *label, create_menu_func func, void *item_context, uint32_t flags);
extern void cbox_menu_item_destroy(struct cbox_menu_item *);

static inline struct cbox_menu_item *cbox_menu_item_new_ok(void)
{
    return cbox_menu_item_new_menu("OK", NULL, NULL, 0);
}

#endif

#endif
