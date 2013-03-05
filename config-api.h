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

#ifndef CBOX_CONFIG_API_H
#define CBOX_CONFIG_API_H

#include <glib.h>

struct cbox_sectref;

extern void cbox_config_init(const char *override_file);
extern int cbox_config_has_section(const char *section);
extern char *cbox_config_get_string(const char *section, const char *key);
extern char *cbox_config_get_string_with_default(const char *section, const char *key, char *def_value);
extern int cbox_config_get_int(const char *section, const char *key, int def_value);
extern float cbox_config_get_float(const char *section, const char *key, float def_value);
extern float cbox_config_get_gain(const char *section, const char *key, float def_value);
extern float cbox_config_get_gain_db(const char *section, const char *key, float def_value);
extern void cbox_config_foreach_section(void (*process)(void *user_data, const char *section), void *user_data);
extern void cbox_config_foreach_key(void (*process)(void *user_data, const char *key), const char *section, void *user_data);
extern char *cbox_config_permify(const char *temporary);

extern void cbox_config_set_string(const char *section, const char *key, const char *value);
extern void cbox_config_set_int(const char *section, const char *key, int value);
extern void cbox_config_set_float(const char *section, const char *key, double value);
extern int cbox_config_remove_section(const char *section);
extern int cbox_config_remove_key(const char *section, const char *key);

extern gboolean cbox_config_save(const char *filename, GError **error);

extern struct cbox_sectref *cbox_config_sectref(struct cbox_sectref *def_sect, const char *prefix, const char *refname);
extern struct cbox_sectref *cbox_config_get_sectref(struct cbox_sectref *sect, const char *prefix, const char *key);
extern struct cbox_sectref *cbox_config_get_sectref_n(struct cbox_sectref *sect, const char *prefix, const char *key, int index);
extern struct cbox_sectref *cbox_config_get_sectref_suffix(struct cbox_sectref *sect, const char *prefix, const char *key, const char *suffix);

extern void cbox_config_close(void);

#endif
