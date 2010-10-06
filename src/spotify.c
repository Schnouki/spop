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

#include <fcntl.h>
#include <glib.h>
#include <libspotify/api.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "spop.h"
#include "plugin.h"
#include "queue.h"
#include "spotify.h"

/************************
 *** Global variables ***
 ************************/
static sp_playlistcontainer* g_container;
static gboolean g_container_loaded = FALSE;

static sp_session* g_session = NULL;

static unsigned int g_audio_time = 0;
static unsigned int g_audio_samples = 0;
static unsigned int g_audio_rate = 44100;

/* Application key -- defined in appkey.c */
extern const uint8_t g_appkey[];
extern const size_t g_appkey_size;


/****************************
 *** Callbacks structures ***
 ****************************/
static sp_playlistcontainer_callbacks g_container_callbacks = {
    NULL,
    NULL,
    NULL,
    &cb_container_loaded
};
static sp_session_callbacks g_session_callbacks = {
    &cb_logged_in,
    &cb_logged_out,
    &cb_metadata_updated,
    &cb_connection_error,
    &cb_message_to_user,
    &cb_notify_main_thread,
    &cb_music_delivery,
    &cb_play_token_lost,
    &cb_log_message,
    &cb_end_of_track
};


/**********************
 *** Init functions ***
 **********************/
void session_init(gboolean high_bitrate) {
    sp_error error;
    sp_session_config config;
    gchar* cache_path;

    /* Cache path */
    cache_path = g_build_filename(g_get_user_cache_dir(), g_get_prgname(), NULL);

    /* libspotify session config */
    config.api_version = SPOTIFY_API_VERSION;
    config.cache_location = cache_path;
    config.settings_location = cache_path;
    config.application_key = g_appkey;
    config.application_key_size = g_appkey_size;
    config.user_agent = "spop " SPOP_VERSION;
    config.callbacks = &g_session_callbacks;

    error = sp_session_init(&config, &g_session);
    if (error != SP_ERROR_OK)
        g_error("Failed to create session: %s", sp_error_message(error));

    /* Set bitrate */
    if (high_bitrate) {
        g_debug("Setting preferred bitrate to high.");
        sp_session_preferred_bitrate(g_session, SP_BITRATE_320k);
    }
    else {
        g_debug("Setting preferred bitrate to low.");
        sp_session_preferred_bitrate(g_session, SP_BITRATE_160k);
    }

    /* Get the playlists container */
    g_container = sp_session_playlistcontainer(g_session);
    if (!g_container)
        g_error("Could not get the playlist container.");

    /* Callback to be able to wait until it is loaded */
    sp_playlistcontainer_add_callbacks(g_container, &g_container_callbacks, NULL);
}

void session_login(const char* username, const char* password) {
    sp_error error;

    if (!g_session)
        g_error("Session is not ready.");

    error = sp_session_login(g_session, username, password);
    if (error != SP_ERROR_OK)
        g_error("Failed to log in: %s", sp_error_message(error));
}


/**************************************************
 *** Functions used from commands and callbacks ***
 **************************************************/
int playlists_len() {
    return sp_playlistcontainer_num_playlists(g_container);
}

sp_playlist* playlist_get(int nb) {
    return sp_playlistcontainer_playlist(g_container, nb);
}

void session_load(sp_track* track) {
    sp_error error;
    
    error = sp_session_player_load(g_session, track);
    if (error != SP_ERROR_OK)
        g_error("Failed to load track: %s", sp_error_message(error));

    cb_notify_main_thread(NULL);
}

void session_unload() {
    sp_session_player_play(g_session, FALSE);
    g_audio_delivery_func(NULL, NULL, 0);
    sp_session_player_unload(g_session);
    cb_notify_main_thread(NULL);
    g_audio_samples = 0;
    g_audio_time = 0;
}

void session_play(gboolean play) {
    sp_error error;

    error = sp_session_player_play(g_session, play);
    if (error != SP_ERROR_OK)
        g_error("Failed to play: %s", sp_error_message(error));

    if (!play)
        /* Force pause in the audio plugin */
        g_audio_delivery_func(NULL, NULL, 0);

    cb_notify_main_thread(NULL);
}

void session_seek(int pos) {
    sp_error error;

    error = sp_session_player_seek(g_session, pos*1000);
    if (error != SP_ERROR_OK)
        g_warning("Seek failed: %s", sp_error_message(error));
    else {
        g_audio_time = pos;
        g_audio_samples = 0;
    }

    cb_notify_main_thread(NULL);
}

int session_play_time() {
    return g_audio_time + (g_audio_samples / g_audio_rate);
}

GArray* tracks_get_playlist(sp_playlist* pl) {
    GArray* tracks;
    sp_track* tr;
    int i, n;

    if (!sp_playlist_is_loaded(pl))
        return NULL;

    n = sp_playlist_num_tracks(pl);
    tracks = g_array_sized_new(FALSE, FALSE, sizeof(sp_track*), n);
    if (!tracks)
        g_error("Can't allocate array of %d tracks.", n);

    for (i=0; i < n; i++) {
        tr = sp_playlist_track(pl, i);
        sp_track_add_ref(tr);
        g_array_append_val(tracks, tr);
    }

    return tracks;
}

void track_get_data(sp_track* track, const char** name, GString** artist, GString** album, GString** link, int* min, int* sec) {
    sp_artist** art = NULL;
    sp_album* alb = NULL;
    sp_link* lnk;
    int dur;
    int i;
    int nb_art = 0;
    const char* s;

    sp_track_add_ref(track);
    if (!sp_track_is_loaded(track)) {
        sp_track_release(track);
        return;
    }

    /* Begin loading everything */
    if (name) {
        *name = sp_track_name(track);
    }
    if (artist) {
        nb_art = sp_track_num_artists(track);
        art = (sp_artist**) malloc(nb_art * sizeof(sp_artist*));
        if (!art)
            g_error("Can't allocate memory.");

        for (i=0; i < nb_art; i++) {
            art[i] = sp_track_artist(track, i);
            sp_artist_add_ref(art[i]);
        }
    }
    if (album) {
        alb = sp_track_album(track);
        sp_album_add_ref(alb);
    }
    if (link) {
        lnk = sp_link_create_from_track(track, 0);
        if (!lnk)
            g_error("Can't get URI from track.");

        *link = g_string_sized_new(1024);
        if (sp_link_as_string(lnk, (*link)->str, 1024) < 0)
            g_error("Can't render URI from link.");

        sp_link_release(lnk);
    }
    if (min || sec) {
        dur = sp_track_duration(track);
        if (min)
            *min = dur/(1000*60);
        if (sec)
            *sec = (dur/1000)%60;
    }

    /* Now create destination strings */
    if (artist) {
        for (i=0; i < nb_art; i++) {
            if (sp_artist_is_loaded(art[i]))
                s = sp_artist_name(art[i]);
            else
                s = "[artist not loaded]";

            if (i == 0) {
                *artist = g_string_new(s);
            }
            else {
                g_string_append(*artist, ", ");
                g_string_append(*artist, s);
            }
            sp_artist_release(art[i]);
        }
    }
    if (album) {
        if (sp_album_is_loaded(alb))
            *album = g_string_new(sp_album_name(alb));
        else
            *album = g_string_new("[album not loaded]");
        sp_album_release(alb);
    }

    sp_track_release(track);
}


/*************************
 *** Utility functions ***
 *************************/
gboolean container_loaded() {
    return g_container_loaded;
}


/*************************
 *** Events management ***
 *************************/
gboolean session_libspotify_event(gpointer data) {
    static guint evid = 0;
    int timeout;

    if (evid > 0)
        g_source_remove(evid);

    do {
        sp_session_process_events(g_session, &timeout);
    } while (timeout == 0);

    /* Add next timeout */
    evid = g_timeout_add(timeout, session_libspotify_event, NULL);

    return FALSE;
}
gboolean session_next_track_event(gpointer data) {
    g_debug("Got next_track event.");
    queue_next(TRUE);

    return FALSE;
}


/******************************************
 *** Callbacks, not to be used directly ***
 ******************************************/
void cb_container_loaded(sp_playlistcontainer* pc, void* data) {
    g_debug("Container loaded.");
    g_container_loaded = TRUE;
}

void cb_logged_in(sp_session* session, sp_error error) {
    if (error != SP_ERROR_OK)
        g_warning("Login failed: %s", sp_error_message(error));
    else g_info("Logged in.");
}

void cb_logged_out(sp_session* session) {
    g_info("Logged out.");
}
void cb_metadata_updated(sp_session* session) {
}

void cb_connection_error(sp_session* session, sp_error error) {
    g_warning("Connection error: %s\n", sp_error_message(error));
}
void cb_message_to_user(sp_session* session, const char* message) {
    g_message("%s", message);
}
void cb_notify_main_thread(sp_session* session) {
    g_idle_add_full(G_PRIORITY_DEFAULT, session_libspotify_event, NULL, NULL);
}
int cb_music_delivery(sp_session* session, const sp_audioformat* format, const void* frames, int num_frames) {
    int n =  g_audio_delivery_func(format, frames, num_frames);

    if (format->sample_rate == g_audio_rate) {
        g_audio_samples += n;
    }
    else if (n > 0) {
        g_audio_time += g_audio_samples / g_audio_rate;
        g_audio_samples = n;
        g_audio_rate = format->sample_rate;
    }

    return n;
}
void cb_play_token_lost(sp_session* session) {
    g_warning("Play token lost.");
}
void cb_log_message(sp_session* session, const char* data) {
    gchar* c = g_strrstr(data, "\n");
    if (c)
        *c = '\0';
    g_log_libspotify("%s", data);
}
void cb_end_of_track(sp_session* session) {
    g_debug("End of track.");
    g_idle_add_full(G_PRIORITY_DEFAULT, session_next_track_event, NULL, NULL);
}
