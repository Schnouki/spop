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

#ifndef CONFIG_H
#define CONFIG_H

typedef enum {
    CONFIG_FOUND,
    CONFIG_NOT_FOUND,
} config_result;


/* Read mandatory options */
int config_get_bool(const char* name);
int config_get_int(const char* name);
const char* config_get_string(const char* name);

/* Read optional options */
config_result config_get_bool_opt(const char* name, int* value);
config_result config_get_int_opt(const char* name, int* value);
config_result config_get_string_opt(const char* name, const char** value);


#endif
