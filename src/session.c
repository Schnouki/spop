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

#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "spop.h"
#include "plugin.h"
#include "session.h"

static sp_session_callbacks g_callbacks = {
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

static sp_session* g_session = NULL;
static int g_timeout = 0;
static sem_t g_notify_sem;

/* Application key -- defined in appkey.c */
extern const uint8_t g_appkey[];
extern const size_t g_appkey_size;

void session_init() {
    sp_error error;
    sp_session_config config;

    /* Semaphore used to notify main thread to process new events */
    sem_init(&g_notify_sem, 0, 0);

    /* libspotify session config
       TODO: change settings and cache path to a real path... */
    config.api_version = SPOTIFY_API_VERSION;
    config.cache_location = "tmp";
    config.settings_location = "tmp";
    config.application_key = g_appkey;
    config.application_key_size = g_appkey_size;
    config.user_agent = "spop " SPOP_VERSION;
    config.callbacks = &g_callbacks;

    error = sp_session_init(&config, &g_session);
    if (error != SP_ERROR_OK) {
        fprintf(stderr, "Failed to create session: %s\n",
                sp_error_message(error));
        exit(1);
    }
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
            sp_session_process_events(g_session, &g_timeout);
        } while (g_timeout == 0);

        /* Wait until either the timeout expires, or the main thread is notified
           using g_notify_sem. */
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += g_timeout / 1000;
        ts.tv_nsec += (g_timeout % 1000) * 1000000;
        /* Stupid overflow? */
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec += ts.tv_nsec / 1000000000;
            ts.tv_nsec %= 1000000000;
        }

         sem_timedwait(&g_notify_sem, &ts);
    }
}

/* Callbacks */
void cb_logged_in(sp_session* session, sp_error error) {
    if (error != SP_ERROR_OK) {
        fprintf(stderr, "Login failed: %s\n",
                sp_error_message(error));
        exit(1);
    }
    else {
        printf("Logged in.\n");
    }
}

void cb_logged_out(sp_session* session) {
    printf("Logged out.\n");
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
    printf("End of track.\n");
}

void* play_sigur_ros(void* bla) {
    static sp_track* track;
    sp_error err;

    while (sp_session_connectionstate(g_session) != SP_CONNECTION_STATE_LOGGED_IN) {
        sleep(1);
    }
    printf("Now trying to play some Sigur Rós...\n");

    sp_link *link = sp_link_create_from_string("spotify:track:6JoAAl9kMpU1ffowg7LrqN");
    if (!link) {
        fprintf(stderr, "Failed to get link from a Spotify URI\n");
        return NULL;
    }

    track = sp_link_as_track(link);
    if (!track) {
        fprintf(stderr, "Not a track link\n");
        sp_link_release(link);
        return NULL;
    }
    while (!sp_track_is_loaded(track)) usleep(10000);

    printf("Load track\n");
    err = sp_session_player_load(g_session, track);
    if (err != SP_ERROR_OK) {
        fprintf(stderr, "Can't load track: %s\n", sp_error_message(err));
        return NULL;
    }
    
    printf("Play track\n");
    err = sp_session_player_play(g_session, 1);
    if (err != SP_ERROR_OK) {
        fprintf(stderr, "Can't play track: %s\n", sp_error_message(err));
        return NULL;
    }

    return NULL;
}
