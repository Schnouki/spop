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

#ifndef QUEUE_H
#define QUEUE_H

#include <glib.h>
#include <libspotify/api.h>

typedef enum { STOPPED, PLAYING, PAUSED } queue_status;

/* Queue management */
void queue_set_track(gboolean notif, sp_track* track);
void queue_add_track(gboolean notif, sp_track* track);

void queue_set_playlist(gboolean notif, sp_playlist* pl);
void queue_add_playlist(gboolean notif, sp_playlist* pl);

void queue_clear(gboolean notif);
void queue_remove_tracks(gboolean notif, int idx, int nb);

/* Playback management */
void queue_play(gboolean notif);
void queue_stop(gboolean notif);
void queue_toggle(gboolean notif);
void queue_seek(int pos);

/* Information about the queue */
queue_status queue_get_status(sp_track** current_track, int* current_track_number, int* total_tracks);
GArray* queue_tracks();

/* Notify clients that something changed */
void queue_notify();

/* Move into the queue */
void queue_next(gboolean notif);
void queue_prev(gboolean notif);
void queue_goto(gboolean notif, int idx, gboolean reset_shuffle_first);

/* Playback mode */
gboolean queue_get_shuffle();
void queue_set_shuffle(gboolean notif, gboolean shuffle);
void queue_setup_shuffle();

gboolean queue_get_repeat();
void queue_set_repeat(gboolean notif, gboolean repeat);

/* Callback functions, not to be used directly */
void cb_queue_track_release(gpointer data, gpointer user_data);

#endif
