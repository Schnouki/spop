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

#ifndef COMMANDS_H
#define COMMANDS_H

#include <glib.h>

/* Commands */
void list_playlists(GString* result);
void list_tracks(GString* result, int idx);

void status(GString* result);
void repeat(GString* result);
void shuffle(GString* result);

void list_queue(GString* result);
void clear_queue(GString* result);
void remove_queue_items(GString* result, int first, int nb);

void play_playlist(GString* result, int idx);
void play_track(GString* result, int pl_idx, int tr_idx);

void add_playlist(GString* result, int idx);
void add_track(GString* result, int pl_idx, int tr_idx);

void play(GString* result);
void toggle(GString* result);
void stop(GString* result);
void seek(GString* result, int pos);

void goto_next(GString* result);
void goto_prev(GString* result);
void goto_nb(GString* result, int nb);

/* Helper functions */
void format_tracks_array(GArray* tracks, GString* dst);

#endif
