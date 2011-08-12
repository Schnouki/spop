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

#ifndef SPOTIFY_H
#define SPOTIFY_H

#include <glib.h>
#include <libspotify/api.h>

/* Init functions */
void session_init(gboolean high_bitrate);
void session_login(const char* username, const char* password);
void session_logout();

/* Playlist management */
int playlists_len();
sp_playlist* playlist_get(int nb);
sp_playlist_type playlist_type(int nb);
gchar* playlist_folder_name(int nb);

/* Session management */
void session_load(sp_track* track);
void session_unload();
void session_play(gboolean play);
void session_seek(int pos);
int session_play_time();

/* Session callbacks management */
typedef enum {
    SPOP_SESSION_LOAD,
    SPOP_SESSION_UNLOAD,
} session_callback_type;
typedef void (*spop_session_callback_ptr)(session_callback_type type, gpointer data, gpointer user_data);
void session_call_callback(gpointer data, gpointer user_data);
gboolean session_add_callback(spop_session_callback_ptr func, gpointer user_data);
gboolean session_remove_callback(spop_session_callback_ptr func, gpointer user_data);

/* Tracks management */
GArray* tracks_get_playlist(sp_playlist* pl);
void track_get_data(sp_track* track, gchar** name, gchar** artist, gchar** album, gchar** link, int* duration);
gboolean track_available(sp_track* track);

sp_image* track_get_image(sp_track* track);
gboolean track_get_image_data(sp_track* track, gpointer* data, gsize* len);

/* Utility functions */
gboolean container_loaded();

/* Events management */
gboolean session_libspotify_event(gpointer data);
gboolean session_next_track_event(gpointer data);

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
