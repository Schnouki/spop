/*
 * Copyright (C) 2010, 2011, 2012, 2013, 2014 Thomas Jost
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

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>

#include "config.h"

/* Mutex for accessing config file */
static GMutex config_mutex;

/* Data structure representing the configuration file */
static GKeyFile* g_config_file = NULL;

/* Internal function: initialize the data structure and load the config file */
static void config_ready() {
    gchar* cfg_path;
    GError* err = NULL;

    g_mutex_lock(&config_mutex);

    if (g_config_file) {
        /* Ready to use the config file */
        g_mutex_unlock(&config_mutex);
        return;
    }

    /* Not ready yet: load the configuration file */
    g_config_file = g_key_file_new();
    if (!g_config_file)
        g_error( "Could not allocate a data structure for reading the configuration file.");

    /* Name of the configuration file. TODO: read path from command-line option. */
    cfg_path = g_build_filename(g_get_user_config_dir(), g_get_prgname(), "spopd.conf", NULL);

    if (!g_key_file_load_from_file(g_config_file, cfg_path, G_KEY_FILE_NONE, &err))
        g_error("Can't read configuration file: %s", err->message);
    g_free(cfg_path);
    g_mutex_unlock(&config_mutex);
}

/* Read options from the config file. To avoid repetitions, this is put in an ugly macro :) */
#define CONFIG_GET_FCT(rtype, dsptype, read_fct, fct_name)              \
    rtype fct_name##_group(const char* group, const char* name) {       \
        rtype value;                                                    \
        GError* err = NULL;                                             \
        config_ready();                                                 \
        value = read_fct(g_config_file, group, name, &err);             \
        if (err)                                                        \
            g_error("Error while reading " dsptype " \"%s::%s\" in configuration file: %s", group, name, err->message); \
        return value;                                                   \
    }                                                                   \
    rtype fct_name(const char* name) {                                  \
        return fct_name##_group(g_get_prgname(), name);                 \
    }                                                                   \
    rtype fct_name##_opt_group(const char* group, const char* name, rtype def_value) { \
        config_ready();                                                 \
        if (g_key_file_has_key(g_config_file, group, name, NULL))       \
            return fct_name##_group(group, name);                       \
        else                                                            \
            return def_value;                                           \
    }                                                                   \
    rtype fct_name##_opt(const char* name, rtype def_value) {           \
        return fct_name##_opt_group(g_get_prgname(), name, def_value);        \
    }                                                                   \
    rtype* fct_name##_list_group(const char* group, const char* name, gsize* length) { \
        rtype* value;                                                   \
        GError* err = NULL;                                             \
        config_ready();                                                 \
        value = (rtype*) read_fct##_list(g_config_file, group, name, length, &err); \
        if (err) {                                                      \
            if (err->code == G_KEY_FILE_ERROR_KEY_NOT_FOUND) {          \
                if (length) *length = 0;                                \
                return NULL;                                            \
            }                                                           \
            else                                                        \
                g_error("Error while reading " dsptype "_list \"%s\" in configuration file: %s", name, err->message); \
        }                                                               \
        return value;                                                   \
    }                                                                   \
    rtype* fct_name##_list(const char* name, gsize* length) {           \
        return fct_name##_list_group(g_get_prgname(), name, length);    \
    }

CONFIG_GET_FCT(gboolean, "boolean", g_key_file_get_boolean, config_get_bool)
CONFIG_GET_FCT(int,      "integer", g_key_file_get_integer, config_get_int)
CONFIG_GET_FCT(gchar*,   "string",  g_key_file_get_string,  config_get_string)
