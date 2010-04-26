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

#ifndef PLAYLIST_H
#define PLAYLIST_H

#include <glib.h>
#include <libspotify/api.h>

/* Functions called directly from spop */
void playlist_init();

/* Utility functions that should not be used from outside of playlist.c */
void container_ready();

/* Commands */
void list_playlists(GString* result);

/* Callbacks */
void cb_container_loaded(sp_playlistcontainer* pc, void* data);
void cb_playlist_added(sp_playlistcontainer* pc, sp_playlist* playlist, int position, void* userdata);
void cb_playlist_removed(sp_playlistcontainer* pc, sp_playlist* playlist, int position, void* userdata);
void cb_playlist_moved(sp_playlistcontainer* pc, sp_playlist* playlist, int position, int new_position, void* userdata);

#endif
