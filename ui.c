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

#include "ui.h"

#include <stdio.h>
#include <ncurses.h>

void cbox_ui_start()
{
    initscr();
    cbreak();
    noecho();
    start_color();    
    keypad(stdscr, TRUE);
}

int cbox_ui_run(struct cbox_ui_page *page)
{
    int ch, res;
    if (page->draw)
        page->draw(page);
    if (page->on_idle)
        halfdelay(1);
    while(1)
    {
        ch = getch();
        if (ch == ERR && page->on_idle)
        {
            res = page->on_idle(page);
            if (res != 0)
                return res;
            continue;
        }
        res = page->on_key(page, ch);
        if (res != 0)
            return res;
    }
    cbreak();
}

void cbox_ui_stop()
{
    endwin();    
}

