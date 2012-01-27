/*
 * Copyright (C) 2010, 2011, 2012 Thomas Jost
 *
 * This file is part of spop.
 *
 * spop is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * spop is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * spop. If not, see <http://www.gnu.org/licenses/>.
 *
 * Additional permission under GNU GPL version 3 section 7
 *
 * If you modify this Program, or any covered work, by linking or combining it
 * with libspotify (or a modified version of that library), containing parts
 * covered by the terms of the Libspotify Terms of Use, the licensors of this
 * Program grant you additional permission to convey the resulting work.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <glib.h>

/* Prototypes for functions used to read options from the config file. To avoid
 * repetitions, this is put in an ugly macro :) */
#define CONFIG_GET_FCT_PROTO(rtype, fct_name)                           \
    rtype fct_name##_group(const char* group, const char* name);        \
    rtype fct_name(const char* name);                                   \
    rtype fct_name##_opt_group(const char* group, const char* name, rtype def_value); \
    rtype fct_name##_opt(const char* name, rtype def_value);            \
    rtype* fct_name##_list_group(const char* group, const char* name, gsize* length); \
    rtype* fct_name##_list(const char* name, gsize* length);

CONFIG_GET_FCT_PROTO(gboolean, config_get_bool)
CONFIG_GET_FCT_PROTO(int, config_get_int)
CONFIG_GET_FCT_PROTO(gchar*, config_get_string)

#endif
