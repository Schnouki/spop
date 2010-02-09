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

#include <libconfig.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "config.h"

/* Mutex for accessing config file */
static pthread_mutex_t config_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Data structure representing the configuration file */
static config_t config_file;
static int config_loaded = 0;

/* Internal function: initialize the data structure and load the config file */
static void config_ready() {
    int res;

    pthread_mutex_lock(&config_mutex);

    if (config_loaded) {
        /* Ready to use the config file */
        pthread_mutex_unlock(&config_mutex);
        return;
    }

    /* Time to get everything ready */
    config_init(&config_file);

    /* TODO: proper path detection & command-line configuration option for the filename */
    res = config_read_file(&config_file, "spopd.conf");
    if (res == CONFIG_TRUE) {
        /* Success! */
        pthread_mutex_unlock(&config_mutex);
        return;
    }
    else {
        /* Failure: try to get an explanation */
        config_error_t err = config_error_type(&config_file);
        if (err == CONFIG_ERR_FILE_IO)
            fprintf(stderr, "I/O error while reading the configuration file\n");
        else {
            fprintf(stderr, "Parse error while reading the configuration file\nIn %s, line %d:\n%s\n",
                    config_error_file(&config_file), config_error_line(&config_file), config_error_text(&config_file));
        }
        pthread_mutex_unlock(&config_mutex);
        exit(1);
    }
}

/* Read mandatory options from the config file */
int config_get_bool(const char* name) {
    int value;

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
config_result config_get_bool_opt(const char* name, int* value) {
    int res;

    config_ready();

    pthread_mutex_lock(&config_mutex);
    res = config_lookup_bool(&config_file, name, value);
    pthread_mutex_unlock(&config_mutex);

    return (res == CONFIG_TRUE) ? CONFIG_FOUND : CONFIG_NOT_FOUND;
}

config_result config_get_int_opt(const char* name, int* value) {
    int res;

    config_ready();

    pthread_mutex_lock(&config_mutex);
    res = config_lookup_int(&config_file, name, value);
    pthread_mutex_unlock(&config_mutex);

    return (res == CONFIG_TRUE) ? CONFIG_FOUND : CONFIG_NOT_FOUND;
}

config_result config_get_string_opt(const char* name, const char** value) {
    int res;

    config_ready();

    pthread_mutex_lock(&config_mutex);
    res = config_lookup_string(&config_file, name, value);
    pthread_mutex_unlock(&config_mutex);

    return (res == CONFIG_TRUE) ? CONFIG_FOUND : CONFIG_NOT_FOUND;
}
