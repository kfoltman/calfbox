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

#ifndef CBOX_SFZPARSER_H
#define CBOX_SFZPARSER_H

#include <glib.h>

#define CBOX_SFZPARSER_ERROR cbox_sfz_parser_error_quark()

enum CboxSfzParserError
{
    CBOX_SFZ_PARSER_ERROR_FAILED,
    CBOX_SFZ_PARSER_ERROR_INVALID_CHAR,
    CBOX_SFZ_PARSER_ERROR_INVALID_HEADER,
};

struct sfz_parser_client
{
    void *user_data;
    void (*region)(struct sfz_parser_client *client);
    void (*group)(struct sfz_parser_client *client);
    gboolean (*key_value)(struct sfz_parser_client *client, const char *key, const char *value);
};

extern gboolean load_sfz(const char *name, struct sfz_parser_client *c, GError **error);
extern gboolean load_sfz_from_string(const char *buf, int len, struct sfz_parser_client *c, GError **error);

extern GQuark cbox_sfz_parser_error_quark();

#endif
