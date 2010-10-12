/*
 * Copyright (C) 2010 Thomas Jost
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
#include <string.h>

#include "utils.h"

/* Replace all occurences of old by new in str */
void g_string_replace(GString* str, const char* old, const gchar* new) {
    gchar* pos_beg = NULL; /* Beginning of the substring where old will be searched */
    gchar* pos_ptr = NULL; /* Position where old is found */

    int len_old, len_new;
    len_old = strlen(old);
    len_new = strlen(new);

    pos_beg = str->str;
    while ((pos_ptr = g_strstr_len(pos_beg,
                                   (str->len - (pos_beg - str->str)),
                                   old)) != NULL) {
        gssize pos = pos_ptr - str->str;
        g_string_erase(str, pos, len_old);
        g_string_insert_len(str, pos, new, len_new); 
        pos_beg = pos_ptr + len_new;
    }
}
