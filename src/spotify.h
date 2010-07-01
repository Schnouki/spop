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

#ifndef SPOTIFY_H
#define SPOTIFY_H

#include <glib.h>
#include <libspotify/api.h>


/* Init functions */
void playlist_init();
void session_init();
void tracks_init();

/* Functions used from commands or callbacks */
int playlists_len();
sp_playlist* playlist_get(int nb);

void session_login(const char* username, const char* password);
void session_events_loop();

sp_playlistcontainer* session_playlistcontainer();

void session_load(sp_track* track);
void session_play(bool play);

GArray* tracks_get_playlist(sp_playlist* pl);
void tracks_add_playlist(sp_playlist* pl);
void tracks_remove_playlist(sp_playlist* pl);
GString* track_get_link(sp_track* track);

/* Utility functions */
void container_ready();
void logged_in();

void playlist_lock();
void playlist_unlock();

void tracks_lock();
void tracks_unlock();

/* Callbacks */
void cb_container_loaded(sp_playlistcontainer* pc, void* data);
void cb_playlist_added(sp_playlistcontainer* pc, sp_playlist* playlist, int position, void* userdata);
void cb_playlist_removed(sp_playlistcontainer* pc, sp_playlist* playlist, int position, void* userdata);
void cb_playlist_moved(sp_playlistcontainer* pc, sp_playlist* playlist, int position, int new_position, void* userdata);

void cb_tracks_added(sp_playlist* pl, sp_track* const* tracks, int num_tracks, int position, void* userdata);
void cb_tracks_removed(sp_playlist* pl, const int* tracks, int num_tracks, void* userdata);
void cb_tracks_moved(sp_playlist* pl, const int* tracks, int num_tracks, int new_position, void* userdata);

void cb_logged_in(sp_session* session, sp_error error);
void cb_logged_out(sp_session* session);
void cb_metadata_updated(sp_session* session);
void cb_connection_error(sp_session* session, sp_error error);
void cb_message_to_user(sp_session* session, const char* message);
void cb_notify_main_thread(sp_session* session);
int cb_music_delivery(sp_session* session, const sp_audioformat* format, const void* frames, int num_frames);
void cb_play_token_lost(sp_session* session);
void cb_log_message(sp_session* session, const char* data);
void cb_end_of_track(sp_session* session);

#endif
