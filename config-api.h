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

extern void cbox_config_init(const char *override_file);
extern int cbox_config_has_section(const char *section);
extern char *cbox_config_get_string(const char *section, const char *key);
extern char *cbox_config_get_string_with_default(const char *section, const char *key, char *def_value);
extern int cbox_config_get_int(const char *section, const char *key, int def_value);
extern float cbox_config_get_float(const char *section, const char *key, float def_value);
extern void cbox_config_foreach_section(void (*process)(void *user_data, const char *key), void *user_data);
extern void cbox_config_close();
