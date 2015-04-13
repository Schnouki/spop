/*
 * Copyright (C) 2010, 2011, 2012, 2013, 2014, 2015 The spop contributors
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

#include "utils.h"

/* Replace all occurences of old by new in str
 * (from https://bugzilla.gnome.org/show_bug.cgi?id=65987) */
void g_string_replace(GString* str, const char* old, const gchar* new) {
    gchar *new_str, **arr;

    arr = g_strsplit(str->str, old, -1);
    if (arr != NULL && arr[0] != NULL)
        new_str = g_strjoinv(new, arr);
    else
        new_str = g_strdup(new);
    g_strfreev(arr);

    g_string_assign(str, new_str);
}

/* Add a line number with a fixed width determined by the greatest possible value */
void g_string_append_line_number(GString* str, int nb, int max_nb) {
    gchar fs[10];
    int nb_digits = 0;
    while (max_nb > 0) { max_nb /= 10; nb_digits += 1; }
    g_snprintf(fs, sizeof(fs), "%%%dd", nb_digits);
    g_string_append_printf(str, fs, nb);
}
