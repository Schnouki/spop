/*
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
 */

#include <glib.h>
#include <libconfig.h>
#include <stdio.h>
#include <stdlib.h>

#include "config.h"

/* Mutex for accessing config file */
static GStaticMutex config_mutex = G_STATIC_MUTEX_INIT;

/* Data structure representing the configuration file */
static config_t config_file;
static int config_loaded = 0;

/* Internal function: initialize the data structure and load the config file */
static void config_ready() {
    int res;
    gchar* cfg_path;

    g_static_mutex_lock(&config_mutex);

    if (config_loaded) {
        /* Ready to use the config file */
        g_static_mutex_unlock(&config_mutex);
        return;
    }

    /* Time to get everything ready */
    config_init(&config_file);

    /* Name of the configuration file. TODO: read path from command-line option. */
    cfg_path = g_build_filename(g_get_user_config_dir(), g_get_prgname(), "spopd.conf", NULL);
    res = config_read_file(&config_file, cfg_path);
    g_free(cfg_path);
    if (res == CONFIG_TRUE) {
        /* Success! */
        g_static_mutex_unlock(&config_mutex);
        return;
    }
    else {
        /* Failure: try to get an explanation */
        config_error_t err = config_error_type(&config_file);
        if (err == CONFIG_ERR_FILE_IO)
            fprintf(stderr, "Could not read the configuration file.\n");
        else {
            fprintf(stderr, "Parse error while reading the configuration file\nIn %s, line %d:\n%s\n",
                    config_error_file(&config_file), config_error_line(&config_file), config_error_text(&config_file));
        }
        g_static_mutex_unlock(&config_mutex);
        exit(1);
    }
}

/* Read mandatory options from the config file */
gboolean config_get_bool(const char* name) {
    gboolean value;

    int res = config_get_bool_opt(name, &value);
    if (res != CONFIG_FOUND) {
        fprintf(stderr, "Missing configuration value: %s\n", name);
        exit(1);
    }

    return value;
}
int config_get_int(const char* name) {
    int value;

    int res = config_get_int_opt(name, &value);
    if (res != CONFIG_FOUND) {
        fprintf(stderr, "Missing configuration value: %s\n", name);
        exit(1);
    }

    return value;
}
const char* config_get_string(const char* name) {
    const char* value;

    int res = config_get_string_opt(name, &value);
    if (res != CONFIG_FOUND) {
        fprintf(stderr, "Missing configuration value: %s\n", name);
        exit(1);
    }

    return value;
}

/* Read optional options from the config file */
config_result config_get_bool_opt(const char* name, gboolean* value) {
    int res;

    config_ready();

    g_static_mutex_lock(&config_mutex);
    res = config_lookup_bool(&config_file, name, value);
    g_static_mutex_unlock(&config_mutex);

    return (res == CONFIG_TRUE) ? CONFIG_FOUND : CONFIG_NOT_FOUND;
}

config_result config_get_int_opt(const char* name, int* value) {
    int res;

    config_ready();

    g_static_mutex_lock(&config_mutex);
    res = config_lookup_int(&config_file, name, value);
    g_static_mutex_unlock(&config_mutex);

    return (res == CONFIG_TRUE) ? CONFIG_FOUND : CONFIG_NOT_FOUND;
}

config_result config_get_string_opt(const char* name, const char** value) {
    int res;

    config_ready();

    g_static_mutex_lock(&config_mutex);
    res = config_lookup_string(&config_file, name, value);
    g_static_mutex_unlock(&config_mutex);

    return (res == CONFIG_TRUE) ? CONFIG_FOUND : CONFIG_NOT_FOUND;
}
