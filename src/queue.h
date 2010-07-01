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

#ifndef QUEUE_H
#define QUEUE_H

#include <glib.h>
#include <libspotify/api.h>

void queue_set_track(sp_track* track);
void queue_add_track(sp_track* track);

void queue_set_playlist(sp_playlist* pl);
void queue_add_playlist(sp_playlist* pl);

void queue_remove_tracks(int idx, int nb);

void queue_play();
void queue_stop();
void queue_toggle();

sp_track* queue_current_track();
void queue_next();
void queue_prev();
void queue_set(int idx);

gboolean queue_get_shuffle();
void queue_set_shuffle(gboolean shuffle);

gboolean queue_get_repeat();
void queue_set_repeat(gboolean repeat);

#endif
