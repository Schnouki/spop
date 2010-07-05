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

#include <glib.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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
static int g_eventloop_timeout = 0;
static sem_t g_notify_sem;
static sem_t g_logged_in_sem;

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
void playlist_init() {
    /* Get the container */
    g_container = sp_session_playlistcontainer(g_session);
    if (g_container == NULL) {
        fprintf(stderr, "Could not get the playlist container\n");
        exit(1);
    }

    /* Callback to be able to wait until it is loaded */
    sp_playlistcontainer_add_callbacks(g_container, &g_container_callbacks, NULL);
}

void session_init(gboolean high_bitrate) {
    sp_error error;
    sp_session_config config;

    /* Semaphore used to notify main thread to process new events */
    sem_init(&g_notify_sem, 0, 0);

    /* Semaphore used to wait until logged in */
    sem_init(&g_logged_in_sem, 0, 0);

    /* libspotify session config
       TODO: change settings and cache path to a real path... */
    config.api_version = SPOTIFY_API_VERSION;
    config.cache_location = "tmp";
    config.settings_location = "tmp";
    config.application_key = g_appkey;
    config.application_key_size = g_appkey_size;
    config.user_agent = "spop " SPOP_VERSION;
    config.callbacks = &g_session_callbacks;

    error = sp_session_init(&config, &g_session);
    if (error != SP_ERROR_OK) {
        fprintf(stderr, "Failed to create session: %s\n",
                sp_error_message(error));
        exit(1);
    }

    /* Set bitrate */
    if (high_bitrate) {
        if (g_debug)
            fprintf(stderr, "Setting preferred bitrate to high.\n");
        sp_session_preferred_bitrate(g_session, SP_BITRATE_320k);
    }
    else {
        if (g_debug)
            fprintf(stderr, "Setting preferred bitrate to low.\n");
        sp_session_preferred_bitrate(g_session, SP_BITRATE_160k);
    }
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

void session_login(const char* username, const char* password) {
    sp_error error;

    if (!g_session) {
        fprintf(stderr, "Session is not ready.\n");
        exit(1);
    }

    error = sp_session_login(g_session, username, password);
    if (error != SP_ERROR_OK) {
        fprintf(stderr, "Failed to log in: %s\n",
                sp_error_message(error));
        exit(1);
    }
}

void session_events_loop() {
    struct timespec ts;

    if (!g_session) {
        fprintf(stderr, "No session\n");
        exit(1);
    }

    while (1) {
        /* If timeout == 0, repeat as many times as needed */
        do {
            sp_session_process_events(g_session, &g_eventloop_timeout);
        } while (g_eventloop_timeout == 0);

        /* Wait until either the timeout expires, or the main thread is notified
           using g_notify_sem. */
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += g_eventloop_timeout / 1000;
        ts.tv_nsec += (g_eventloop_timeout % 1000) * 1000000;
        /* Stupid overflow? */
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec += ts.tv_nsec / 1000000000;
            ts.tv_nsec %= 1000000000;
        }

         sem_timedwait(&g_notify_sem, &ts);
    }
}

void session_load(sp_track* track) {
    sp_error error;
    
    error = sp_session_player_load(g_session, track);
    if (error != SP_ERROR_OK) {
        fprintf(stderr, "Failed to load track: %s\n",
                sp_error_message(error));
        exit(1);
    }
}

void session_unload() {
    sp_session_player_unload(g_session);
    g_audio_samples = 0;
    g_audio_time = 0;
}

void session_play(gboolean play) {
    sp_error error;

    error = sp_session_player_play(g_session, play);
    if (error != SP_ERROR_OK) {
        fprintf(stderr, "Failed to play: %s\n",
                sp_error_message(error));
        exit(1);
    }
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
    for (i=0; i < n; i++) {
        tr = sp_playlist_track(pl, i);
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

    /* Begin loading everything */
    if (name) {
        *name = sp_track_name(track);
    }
    if (artist) {
        nb_art = sp_track_num_artists(track);
        art = (sp_artist**) malloc(nb_art * sizeof(sp_artist*));
        if (!art) {
            fprintf(stderr, "Can't allocate memory\n");
            exit(1);
        }

        for (i=0; i < nb_art; i++)
            art[i] = sp_track_artist(track, i);
    }
    if (album) {
        alb = sp_track_album(track);
    }
    if (link) {
        lnk = sp_link_create_from_track(track, 0);
        if (!lnk) {
            fprintf(stderr, "Can't get URI from track\n");
            exit(1);
        }
        *link = g_string_sized_new(1024);
        if (sp_link_as_string(lnk, (*link)->str, 1024) < 0) {
            fprintf(stderr, "Can't render URI from link\n");
            exit(1);
        }
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
            while (!sp_artist_is_loaded(art[i])) { usleep(10000); }
            if (i == 0) {
                *artist = g_string_new(sp_artist_name(art[i]));
            }
            else {
                g_string_append(*artist, ", ");
                g_string_append(*artist, sp_artist_name(art[i]));
            }
        }
    }
    if (album) {
        while (!sp_album_is_loaded(alb)) { usleep(10000); }
        *album = g_string_new(sp_album_name(alb));
    }
}



/*************************
 *** Utility functions ***
 *************************/
gboolean container_loaded() {
    return g_container_loaded;
}
void logged_in() {
    sem_wait(&g_logged_in_sem);
    sem_post(&g_logged_in_sem);
}


/******************************************
 *** Callbacks, not to be used directly ***
 ******************************************/
void cb_container_loaded(sp_playlistcontainer* pc, void* data) {
    if (g_debug)
        fprintf(stderr, "Container loaded.\n");
    g_container_loaded = TRUE;
}

void cb_logged_in(sp_session* session, sp_error error) {
    if (error != SP_ERROR_OK) {
        fprintf(stderr, "Login failed: %s\n",
                sp_error_message(error));
        exit(1);
    }
    else {
        if (g_debug)
            fprintf(stderr, "Logged in.\n");
        sem_post(&g_logged_in_sem);
    }
}

void cb_logged_out(sp_session* session) {
    if (g_debug)
        fprintf(stderr, "Logged out.\n");
    exit(1);
}
void cb_metadata_updated(sp_session* session) {
}

void cb_connection_error(sp_session* session, sp_error error) {
    fprintf(stderr, "Connection error: %s\n", sp_error_message(error));
}
void cb_message_to_user(sp_session* session, const char* message) {
    printf("Message from Spotify: %s\n", message);
}
void cb_notify_main_thread(sp_session* session) {
    /* Wake up main thread using a semaphore */
    sem_post(&g_notify_sem);
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
    fprintf(stderr, "Play token lost.\n");
}
void cb_log_message(sp_session* session, const char* data) {
    fprintf(stderr, data);
}
void cb_end_of_track(sp_session* session) {
    if (g_debug)
        fprintf(stderr, "End of track.\n");
    queue_next();
}
