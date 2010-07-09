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
void session_init(gboolean high_bitrate);
void session_login(const char* username, const char* password);

/* Functions used from commands or callbacks */
int playlists_len();
sp_playlist* playlist_get(int nb);

sp_playlistcontainer* session_playlistcontainer();

void session_load(sp_track* track);
void session_unload();
void session_play(gboolean play);
void session_seek(int pos);
int session_play_time();

GArray* tracks_get_playlist(sp_playlist* pl);

void track_get_data(sp_track* track, const char** name, GString** artist, GString** album, GString** link, int* min, int* sec);

/* Utility functions */
gboolean container_loaded();

/* Events management */
gboolean session_event(gpointer data);

/* Callbacks */
void cb_container_loaded(sp_playlistcontainer* pc, void* data);

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
