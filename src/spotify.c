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
#include "spotify.h"

/************************
 *** Global variables ***
 ************************/
static sp_playlistcontainer* g_container;
static sem_t g_container_loaded_sem;

static GArray* g_playlists;
static GStaticRWLock g_playlist_lock = G_STATIC_RW_LOCK_INIT;

static GHashTable* g_playlist_tracks;
static GStaticRWLock g_playlist_tracks_lock = G_STATIC_RW_LOCK_INIT;

static sp_session* g_session = NULL;
static int g_eventloop_timeout = 0;
static sem_t g_notify_sem;
static sem_t g_logged_in_sem;

/* Application key -- defined in appkey.c */
extern const uint8_t g_appkey[];
extern const size_t g_appkey_size;


/****************************
 *** Callbacks structures ***
 ****************************/
static sp_playlistcontainer_callbacks g_container_callbacks = {
    &cb_playlist_added,
    &cb_playlist_removed,
    &cb_playlist_moved,
    &cb_container_loaded
};
static sp_playlist_callbacks g_playlist_callbacks = {
    &cb_tracks_added,
    &cb_tracks_removed,
    &cb_tracks_moved,
    NULL,
    NULL,
    NULL,
    NULL
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
    /* Semaphore used to determine if the playlist container is loaded */
    sem_init(&g_container_loaded_sem, 0, 0);

    /* Init the playlists sequence */
    g_static_rw_lock_writer_lock(&g_playlist_lock);
    g_playlists = g_array_new(FALSE, TRUE, sizeof(sp_playlist*));
    g_static_rw_lock_writer_unlock(&g_playlist_lock);

    /* Get the container */
    g_container = sp_session_playlistcontainer(g_session);
    if (g_container == NULL) {
        fprintf(stderr, "Could not get the playlist container\n");
        exit(1);
    }

    /* Callback to be able to wait until it is loaded */
    sp_playlistcontainer_add_callbacks(g_container, &g_container_callbacks, NULL);
}

void session_init() {
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
}

void tracks_init() {
    g_static_rw_lock_writer_lock(&g_playlist_tracks_lock);
    g_playlist_tracks = g_hash_table_new(NULL, NULL);
    g_static_rw_lock_writer_unlock(&g_playlist_tracks_lock);
}

/**************************************************
 *** Functions used from commands and callbacks ***
 **************************************************/
int playlists_len() {
    int len;
    g_static_rw_lock_reader_lock(&g_playlist_lock);
    len = g_playlists->len;
    g_static_rw_lock_reader_unlock(&g_playlist_lock);

    return len;
}

sp_playlist* playlist_get(int nb) {
    sp_playlist* pl = NULL;

    g_static_rw_lock_reader_lock(&g_playlist_lock);
    if ((nb >= 0) && (nb < g_playlists->len))
        pl = g_array_index(g_playlists, sp_playlist*, nb);
    g_static_rw_lock_reader_unlock(&g_playlist_lock);

    return pl;
}

void session_login(const char* username, const char* password) {
    sp_error error;

    if (g_session == NULL) session_init();

    error = sp_session_login(g_session, username, password);
    if (error != SP_ERROR_OK) {
        fprintf(stderr, "Failed to log in: %s\n",
                sp_error_message(error));
        exit(1);
    }
}

void session_events_loop() {
    struct timespec ts;

    if (g_session == NULL) {
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

void session_play(bool play) {
    sp_error error;

    error = sp_session_player_play(g_session, play);
    if (error != SP_ERROR_OK) {
        fprintf(stderr, "Failed to play: %s\n",
                sp_error_message(error));
        exit(1);
    }
}

GArray* tracks_get_playlist(sp_playlist* pl) {
    return g_hash_table_lookup(g_playlist_tracks, pl);
}

void tracks_add_playlist(sp_playlist* pl) {
    GArray* tracks;
    int nb;

    /* Number of tracks */
    nb = sp_playlist_num_tracks(pl);
    
    /* Get or create array */
    g_static_rw_lock_writer_lock(&g_playlist_tracks_lock);
    tracks = g_hash_table_lookup(g_playlist_tracks, pl);
    if (tracks == NULL) {
        tracks = g_array_sized_new(FALSE, TRUE, sizeof(sp_track*), nb);
        g_hash_table_insert(g_playlist_tracks, pl, tracks);
    }
    g_static_rw_lock_writer_unlock(&g_playlist_tracks_lock);
}

void tracks_remove_playlist(sp_playlist* pl) {
    g_static_rw_lock_writer_lock(&g_playlist_tracks_lock);
    g_hash_table_remove(g_playlist_tracks, pl);
    g_static_rw_lock_writer_unlock(&g_playlist_tracks_lock);
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
void container_ready() {
    sem_wait(&g_container_loaded_sem);
    sem_post(&g_container_loaded_sem);
}
void logged_in() {
    sem_wait(&g_logged_in_sem);
    sem_post(&g_logged_in_sem);
}

void playlist_lock() {
    g_static_rw_lock_reader_lock(&g_playlist_lock);
}
void playlist_unlock() {
    g_static_rw_lock_reader_unlock(&g_playlist_lock);
}

void tracks_lock() {
    g_static_rw_lock_reader_lock(&g_playlist_tracks_lock);
}
void tracks_unlock() {
    g_static_rw_lock_reader_unlock(&g_playlist_tracks_lock);
}


/******************************************
 *** Callbacks, not to be used directly ***
 ******************************************/
void cb_container_loaded(sp_playlistcontainer* pc, void* data) {
    int i, np;
    sp_playlist* pl;

    np = sp_playlistcontainer_num_playlists(pc);
    if (np == -1) {
        fprintf(stderr, "Could not determine the number of playlists\n");
        exit(1);
    }

    /* Begin loading the playlists */
    g_static_rw_lock_writer_lock(&g_playlist_lock);
    g_array_set_size(g_playlists, np);
    for (i=0; i < np; i++) {
        pl = sp_playlistcontainer_playlist(pc, i);
        g_array_insert_val(g_playlists, i, pl);
    }
    g_static_rw_lock_writer_unlock(&g_playlist_lock);

    sem_post(&g_container_loaded_sem);
}
void cb_playlist_added(sp_playlistcontainer* pc, sp_playlist* playlist, int position, void* userdata) {
    if (g_debug)
        fprintf(stderr, "Adding playlist %d.\n", position);

    g_static_rw_lock_writer_lock(&g_playlist_lock);
    g_array_insert_val(g_playlists, position, playlist);
    g_static_rw_lock_writer_unlock(&g_playlist_lock);
    tracks_add_playlist(playlist);
    sp_playlist_add_callbacks(playlist, &g_playlist_callbacks, NULL);
}
void cb_playlist_removed(sp_playlistcontainer* pc, sp_playlist* playlist, int position, void* userdata) {
    if (g_debug)
        fprintf(stderr, "Removing playlist %d.\n", position);

    g_static_rw_lock_writer_lock(&g_playlist_lock);
    g_array_remove_index(g_playlists, position);
    g_static_rw_lock_writer_unlock(&g_playlist_lock);
    tracks_remove_playlist(playlist);
}
void cb_playlist_moved(sp_playlistcontainer* pc, sp_playlist* playlist, int position, int new_position, void* userdata) {
    if (g_debug)
        fprintf(stderr, "Moving playlist %d to %d.\n", position, new_position);

    g_static_rw_lock_writer_lock(&g_playlist_lock);
    g_array_remove_index(g_playlists, position);
    g_array_insert_val(g_playlists, position, playlist);
    g_static_rw_lock_writer_unlock(&g_playlist_lock);
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
    return g_audio_delivery_func(format, frames, num_frames);
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
}

void cb_tracks_added(sp_playlist* pl, sp_track* const* tracks, int num_tracks, int position, void* userdata) {
    GArray* ta;
    int i;

    /* Get tracks array */
    g_static_rw_lock_writer_lock(&g_playlist_tracks_lock);
    ta = g_hash_table_lookup(g_playlist_tracks, pl);
    if (ta == NULL) {
        fprintf(stderr, "Can't find tracks array\n");
        exit(1);
    }

    /* Insert tracks in array */
    for (i=0; i < num_tracks; i++)
        g_array_insert_val(ta, position+i, tracks[i]);

    g_static_rw_lock_writer_unlock(&g_playlist_tracks_lock);

    if (g_debug)
        fprintf(stderr, "Added %d tracks at position %d.\n", num_tracks, position);
}
void cb_tracks_removed(sp_playlist* pl, const int* tracks, int num_tracks, void* userdata) {
    GArray* ta;
    int i;

    /* Get tracks array */
    g_static_rw_lock_writer_lock(&g_playlist_tracks_lock);
    ta = g_hash_table_lookup(g_playlist_tracks, pl);
    if (ta == NULL) {
        fprintf(stderr, "Can't find tracks array\n");
        exit(1);
    }

    /* Remove tracks from array */
    for (i=num_tracks-1; i >= 0; i--)
        g_array_remove_index(ta, tracks[i]);

    g_static_rw_lock_writer_unlock(&g_playlist_tracks_lock);

    if (g_debug)
        fprintf(stderr, "Removed %d tracks.\n", num_tracks);
}
void cb_tracks_moved(sp_playlist* pl, const int* tracks, int num_tracks, int new_position, void* userdata) {
    GArray* ta;
    GArray* tmp;
    sp_track* track;
    int i;

    /* Get tracks array */
    g_static_rw_lock_writer_lock(&g_playlist_tracks_lock);
    ta = g_hash_table_lookup(g_playlist_tracks, pl);
    if (ta == NULL) {
        fprintf(stderr, "Can't find tracks array\n");
        exit(1);
    }

    /* Array of tracks to be moved */
    tmp = g_array_sized_new(FALSE, TRUE, sizeof(sp_track*), num_tracks);
    for (i=0; i < num_tracks; i++) {
        track = g_array_index(ta, sp_track*, tracks[i]);
        g_array_insert_val(tmp, i, track);
    }

    /* Insert tracks at their new position */
    g_array_insert_vals(ta, new_position, tmp->data, num_tracks);

    /* Remove tracks from tracks array */
    for (i=num_tracks-1; i >= 0; i--)
        g_array_remove_index(ta, num_tracks + tracks[i]);

    g_static_rw_lock_writer_unlock(&g_playlist_tracks_lock);

    /* Free tmp array */
    g_array_free(tmp, TRUE);

    if (g_debug)
        fprintf(stderr, "Moved %d tracks to position %d.\n", num_tracks, new_position);
}
