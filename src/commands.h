/*
 * Copyright (C) 2010, 2011 Thomas Jost
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

#ifndef COMMANDS_H
#define COMMANDS_H

#include <glib.h>
#include <json-glib/json-glib.h>

/* Commands */
void list_playlists(JsonBuilder* jb);
void list_tracks(JsonBuilder* jb, const gchar* idx);

void status(JsonBuilder* jb);
void repeat(JsonBuilder* jb);
void shuffle(JsonBuilder* jb);

void list_queue(JsonBuilder* jb);
void clear_queue(JsonBuilder* jb);
void remove_queue_items(JsonBuilder* jb, const gchar* first, const gchar* last);
void remove_queue_item(JsonBuilder* jb, const gchar* idx);

void play_playlist(JsonBuilder* jb, const gchar* idx);
void play_track(JsonBuilder* jb, const gchar* pl_idx, const gchar* tr_idx);

void add_playlist(JsonBuilder* jb, const gchar* idx);
void add_track(JsonBuilder* jb, const gchar* pl_idx, const gchar* tr_idx);

void play(JsonBuilder* jb);
void toggle(JsonBuilder* jb);
void stop(JsonBuilder* jb);
void seek(JsonBuilder* jb, const gchar* pos);

void goto_next(JsonBuilder* jb);
void goto_prev(JsonBuilder* jb);
void goto_nb(JsonBuilder* jb, const gchar* nb);

void image(JsonBuilder* jb);

#endif
