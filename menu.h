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

#ifndef CBOX_MENU_H
#define CBOX_MENU_H

#include <ncurses.h>
#include <stdint.h>

#include "menuitem.h"
#include "ui.h"

struct cbox_menu;
struct cbox_menu_item;
struct cbox_menu_page;

struct cbox_menu_state
{
    struct cbox_menu_page *page;
    struct cbox_menu *menu;
    int cursor;
    struct cbox_menu_measure size;
    WINDOW *window;
    void *context;
    struct cbox_menu_state *caller;
    int menu_is_temporary;
};

extern struct cbox_menu *cbox_menu_new();
extern struct cbox_menu_item *cbox_menu_add_item(struct cbox_menu *menu, struct cbox_menu_item *item);
extern void cbox_menu_destroy(struct cbox_menu *menu);

extern struct cbox_menu_state *cbox_menu_state_new(struct cbox_menu_page *page, struct cbox_menu *menu, WINDOW *window, void *context);
extern void cbox_menu_state_destroy(struct cbox_menu_state *st);

struct cbox_menu_page
{
    struct cbox_ui_page page;
    struct cbox_menu_state *state;
};

extern struct cbox_menu_page *cbox_menu_page_new();
extern void cbox_menu_page_destroy(struct cbox_menu_page *st);

#endif
