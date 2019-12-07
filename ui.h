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

#ifndef CBOX_UI_H
#define CBOX_UI_H

#include "config.h"

#if USE_NCURSES

struct cbox_ui_page
{
    void *user_data;
    void (*draw)(struct cbox_ui_page *page);
    int (*on_key)(struct cbox_ui_page *page, int ch);
    int (*on_idle)(struct cbox_ui_page *page);
};

extern void cbox_ui_start(void);
extern int cbox_ui_run(struct cbox_ui_page *page);
extern void cbox_ui_stop(void);

#endif

#endif
