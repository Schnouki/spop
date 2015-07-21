/*
 * Copyright (C) 2010, 2011, 2012, 2013, 2014, 2015 The spop contributors
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

#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <libspotify/api.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "spop.h"
#include "config.h"
#include "plugin.h"
#include "queue.h"
#include "spotify.h"

/************************
 *** Global variables ***
 ************************/
static sp_playlistcontainer* g_container;
static sp_playlist* g_starred_playlist = NULL;

static sp_session* g_session = NULL;

static guint g_audio_time = 0;
static unsigned int g_audio_samples = 0;
static unsigned int g_audio_rate = 44100;

/* Session load/unload callbacks */
static GList* g_session_callbacks = NULL;
typedef struct {
    spop_session_callback_ptr func;
    gpointer user_data;
} session_callback;
typedef struct {
    session_callback_type type;
    gpointer data;
} session_callback_data;

/* Application key -- defined in appkey.c */
extern const uint8_t g_appkey[];
extern const size_t g_appkey_size;


/****************************
 *** Callbacks structures ***
 ****************************/
static sp_session_callbacks g_sp_session_callbacks = {
    &cb_logged_in,
    &cb_logged_out,
    &cb_metadata_updated,
    &cb_connection_error,
    &cb_message_to_user,
    &cb_notify_main_thread,
    &cb_music_delivery,
    &cb_play_token_lost,
    &cb_log_message,
    &cb_end_of_track,
    &cb_streaming_error,
    NULL, /* userinfo_updated */
    NULL, /* start_playback */
    NULL, /* stop_playback */
    NULL, /* get_audio_buffer_stats */
    NULL, /* offline_status_updated */
    NULL, /* offline_error */
    NULL, /* credentials_blob_updated */
    NULL, /* connectionstate_updated */
    NULL, /* scrobble_error */
    NULL  /* private_session_mode_changed */
};


/**********************
 *** Init functions ***
 **********************/
void session_init() {
    sp_error error;
    gchar* cache_path;
    gchar* settings_path;
    gchar* proxy;
    gchar* proxy_username;
    gchar* proxy_password;

    g_debug("Creating session...");

    /* Cache and settings path */
    cache_path = config_get_string_opt("cache_path", NULL);
    if (!cache_path)
        cache_path = g_build_filename(g_get_user_cache_dir(), g_get_prgname(), NULL);

    /* The cache path is not automatically created by libspotify */
    if (g_mkdir_with_parents(cache_path, 0700) != 0) {
        g_error("Can't create the cache path %s: %s", cache_path, g_strerror(errno));
    }

    settings_path = config_get_string_opt("settings_path", NULL);
    if (!settings_path)
        settings_path = g_build_filename(g_get_user_cache_dir(), g_get_prgname(), NULL);

    /* The settings path is not automatically created by libspotify */
    if (g_mkdir_with_parents(settings_path, 0700) != 0) {
        g_error("Can't create the settings path %s: %s", settings_path, g_strerror(errno));
    }

    /* libspotify session config */
    if (g_audio_buffer_stats_func)
        g_sp_session_callbacks.get_audio_buffer_stats = g_audio_buffer_stats_func;
    proxy = config_get_string_opt("proxy", NULL);
    proxy_username = config_get_string_opt("proxy_username", NULL);
    proxy_password = config_get_string_opt("proxy_password", NULL);

    sp_session_config config = {
        .api_version = SPOTIFY_API_VERSION,
        .cache_location = cache_path,
        .settings_location = settings_path,
        .application_key = g_appkey,
        .application_key_size = g_appkey_size,
        .user_agent = "spop " SPOP_VERSION,
        .callbacks = &g_sp_session_callbacks,
        .userdata = NULL,
        .compress_playlists = FALSE,
        .dont_save_metadata_for_playlists = FALSE,
        .initially_unload_playlists = FALSE,
        .proxy = proxy,
        .proxy_username = proxy_username,
        .proxy_password = proxy_password,
        NULL,
    };

    error = sp_session_create(&config, &g_session);
    if (error != SP_ERROR_OK)
        g_error("Failed to create session: %s", sp_error_message(error));

    /* Set bitrate */
    if (config_get_bool_opt("high_bitrate", TRUE)) {
        g_debug("Setting preferred bitrate to high.");
        sp_session_preferred_bitrate(g_session, SP_BITRATE_320k);
    }
    else {
        g_debug("Setting preferred bitrate to low.");
        sp_session_preferred_bitrate(g_session, SP_BITRATE_160k);
    }
    if (config_get_bool_opt("offline_high_bitrate", TRUE)) {
        g_debug("Setting preferred offline bitrate to high.");
        sp_session_preferred_offline_bitrate(g_session, SP_BITRATE_320k, FALSE);
    }
    else {
        g_debug("Setting preferred offline bitrate to low.");
        sp_session_preferred_offline_bitrate(g_session, SP_BITRATE_160k, FALSE);
    }

    size_t cache_size = config_get_int_opt("cache_size", 0);
    g_debug("Setting cache size to %zu.", cache_size);
    sp_session_set_cache_size(g_session, cache_size);

    gboolean normalize_volume = config_get_bool_opt("normalize_volume", TRUE);
    g_debug("%s volume normalization.", normalize_volume ? "Enabling" : "Disabling");
    sp_session_set_volume_normalization(g_session, normalize_volume);

    g_debug("Session created.");
}

void session_login(const char* username, const char* password) {
    g_debug("Logging in...");
    if (!g_session)
        g_error("Session is not ready.");

    sp_session_login(g_session, username, password, TRUE, NULL);
}
void session_logout() {
    g_debug("Logging out...");
    if (g_session)
        sp_session_logout(g_session);
}


/***************************
 *** Playlist management ***
 ***************************/
int playlists_len() {
    return sp_playlistcontainer_num_playlists(g_container) + 1; /* +1 for "starred" playlist */
}

sp_playlist* playlist_get(int nb) {
    if (nb == 0) {
        if (g_starred_playlist == NULL)
            g_starred_playlist = sp_session_starred_create(g_session);
        return g_starred_playlist;
    }
    else
        return sp_playlistcontainer_playlist(g_container, nb-1);
}

sp_playlist* playlist_get_from_link(sp_link* lnk) {
    return sp_playlist_create(g_session, lnk);
}

sp_playlist_type playlist_type(int nb) {
    if (nb == 0)
        return SP_PLAYLIST_TYPE_PLAYLIST;
    else
        return sp_playlistcontainer_playlist_type(g_container, nb-1);
}

gchar* playlist_folder_name(int nb) {
    sp_error error;
    gchar* name;

    if (nb == 0)
        name = g_strdup("Starred");
    else {
        gsize len = 512 * sizeof(gchar);
        name = g_malloc(len);
        error = sp_playlistcontainer_playlist_folder_name(g_container, nb-1, name, len);
        if (error != SP_ERROR_OK)
            g_error("Failed to get playlist folder name: %s", sp_error_message(error));
    }

    return name;
}

sp_playlist_offline_status playlist_get_offline_status(sp_playlist* pl) {
    return sp_playlist_get_offline_status(g_session, pl);
}

void playlist_set_offline_mode(sp_playlist* pl, gboolean mode) {
    sp_playlist_set_offline_mode(g_session, pl, mode);
}

int playlist_get_offline_download_completed(sp_playlist* pl) {
    return sp_playlist_get_offline_download_completed(g_session, pl);
}

/**********************
 * Session management *
 **********************/
void session_load(sp_track* track) {
    sp_error error;
    session_callback_data scbd;

    g_debug("Loading track.");

    error = sp_session_player_load(g_session, track);
    if (error != SP_ERROR_OK)
        g_error("Failed to load track: %s", sp_error_message(error));

    /* Queue some events management */
    cb_notify_main_thread(NULL);

    /* Then call callbacks */
    scbd.type = SPOP_SESSION_LOAD;
    scbd.data = track;
    g_list_foreach(g_session_callbacks, session_call_callback, &scbd);
}

void session_unload() {
    session_callback_data scbd;

    g_debug("Unloading track.");

    /* First call callbacks */
    scbd.type = SPOP_SESSION_UNLOAD;
    scbd.data = NULL;
    g_list_foreach(g_session_callbacks, session_call_callback, &scbd);

    /* Then really unload */
    sp_session_player_play(g_session, FALSE);
    g_audio_delivery_func(NULL, NULL, 0);
    sp_session_player_unload(g_session);
    cb_notify_main_thread(NULL);
    g_audio_samples = 0;
    g_audio_time = 0;
}

void session_play(gboolean play) {
    sp_session_player_play(g_session, play);

    if (!play)
        /* Force pause in the audio plugin */
        g_audio_delivery_func(NULL, NULL, 0);

    cb_notify_main_thread(NULL);
}

void session_seek(guint pos) {
    sp_session_player_seek(g_session, pos);
    g_audio_time = pos;
    g_audio_samples = 0;

    cb_notify_main_thread(NULL);

    queue_notify();
}

guint session_play_time() {
    /* If there are more that 4,294,967 samples, (1000 * g_audio_samples) will
     * overflow a 32 bit integer. So let's do some floating-point math
     * instead. */
    gfloat time = g_audio_time + (1000. * g_audio_samples) / g_audio_rate;
    return (guint) time;
}

void session_get_offline_sync_status(sp_offline_sync_status* status, gboolean* sync_in_progress,
                                     int* tracks_to_sync, int* num_playlists, int* time_left) {
    if (status || sync_in_progress) {
        sp_offline_sync_status oss;
        gboolean sip = sp_offline_sync_get_status(g_session, &oss);
        if (status)
            *status = oss;
        if (sync_in_progress)
            *sync_in_progress = sip;
    }
    if (tracks_to_sync)
        *tracks_to_sync = sp_offline_tracks_to_sync(g_session);
    if (num_playlists)
        *num_playlists = sp_offline_num_playlists(g_session);
    if (time_left)
        *time_left = sp_offline_time_left(g_session);
}

/********************************
 * Session callbacks management *
 ********************************/
void session_call_callback(gpointer data, gpointer user_data) {
    session_callback* scb = (session_callback*) data;
    session_callback_data* scbd = (session_callback_data*) user_data;

    scb->func(scbd->type, scbd->data, scb->user_data);
}

gboolean session_add_callback(spop_session_callback_ptr func, gpointer user_data) {
    session_callback* scb;
    GList* cur;

    /* Is there already such a callback/data couple in the list? */
    cur = g_session_callbacks;
    while (cur != NULL) {
        scb = cur->data;
        if ((scb->func == func) && (scb->user_data == user_data))
            return FALSE;
        cur = cur->next;
    }

    /* Callback/data not in the list: add them */
    scb = g_malloc(sizeof(session_callback));
    scb->func = func;
    scb->user_data = user_data;
    g_session_callbacks = g_list_prepend(g_session_callbacks, scb);
    return TRUE;
}

gboolean session_remove_callback(spop_session_callback_ptr func, gpointer user_data) {
    session_callback* scb;
    GList* cur;

    /* Try to find the callback/data couple in the list */
    cur = g_session_callbacks;
    while (cur != NULL) {
        scb = cur->data;
        if ((scb->func == func) && (scb->user_data == user_data)) {
            g_free(scb);
            g_session_callbacks = g_list_delete_link(g_session_callbacks, cur);
            return TRUE;
        }
        cur = cur->next;
    }

    /* Not found */
    return FALSE;
}


/*********************
 * Tracks management *
 *********************/
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

void track_get_data(sp_track* track, gchar** name, gchar** artist, gchar** album, gchar** link,
                    guint* duration, int* popularity, bool* starred) {
    sp_artist** art = NULL;
    sp_album* alb = NULL;
    sp_link* lnk;
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
        *name = g_strdup(sp_track_name(track));
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
        GString* tmp;
        lnk = sp_link_create_from_track(track, 0);
        if (!lnk)
            g_error("Can't get URI from track.");

        tmp = g_string_sized_new(1024);
        if (sp_link_as_string(lnk, tmp->str, 1024) < 0)
            g_error("Can't render URI from link.");
        *link = tmp->str;
        g_string_free(tmp, FALSE);

        sp_link_release(lnk);
    }
    if (duration) {
        *duration = sp_track_duration(track);
    }
    if (popularity) {
        *popularity = sp_track_popularity(track);
    }
    if (starred) {
        *starred = sp_track_is_starred(g_session, track);
    }

    /* Now create destination strings */
    if (artist) {
        GString* tmp = g_string_new("");
        for (i=0; i < nb_art; i++) {
            if (sp_artist_is_loaded(art[i]))
                s = sp_artist_name(art[i]);
            else
                s = "[artist not loaded]";

            if (i != 0)
                g_string_append(tmp, ", ");
            g_string_append(tmp, s);
            sp_artist_release(art[i]);
        }
        *artist = tmp->str;
        g_string_free(tmp, FALSE);
    }
    if (album) {
        if (sp_album_is_loaded(alb))
            *album = g_strdup(sp_album_name(alb));
        else
            *album = g_strdup("[album not loaded]");
        sp_album_release(alb);
    }

    sp_track_release(track);
}

gboolean track_available(sp_track* track) {
    return (sp_track_get_availability(g_session, track) == SP_TRACK_AVAILABILITY_AVAILABLE);
}

void track_set_starred(sp_track** tracks, gboolean starred) {
    size_t size = 0;
    while (tracks[size] != NULL)
        size += 1;
    if (size == 0)
        return;
    sp_error error = sp_track_set_starred(g_session, tracks, size, starred);
    if (error != SP_ERROR_OK)
        g_warning("Failed to set track starred status: %s", sp_error_message(error));
}

sp_image* image_id_get_image(const void* img_id) {
  return sp_image_create(g_session, img_id);
}

sp_image* track_get_image(sp_track* track) {
    sp_album* alb = NULL;
    sp_image* img = NULL;
    const void* img_id = NULL;

    /* Get album */
    alb = sp_track_album(track);
    if (!alb)
        g_error("Can't get track album.");
    sp_album_add_ref(alb);

    if (!sp_album_is_loaded(alb))
        g_error("Album not loaded.");

    /* Get image */
    img_id = sp_album_cover(alb, SP_IMAGE_SIZE_NORMAL);
    if (!img_id) {
        /* Since the album is loaded, a NULL here indicates that there is no
           cover for this album. */
        sp_album_release(alb);
        return NULL;
    }

    img = sp_image_create(g_session, img_id);
    sp_album_release(alb);

    if (!img)
        g_error("Can't create image.");
    return img;
}

gboolean track_get_image_data(sp_track* track, gpointer* data, gsize* len) {
    sp_image* img;
    const guchar* img_data = NULL;

    img = track_get_image(track);
    if (!img) {
        /* No cover */
        *data = NULL;
        *len = 0;
        return TRUE;
    }

    if (!sp_image_is_loaded(img))
        return FALSE;

    img_data = sp_image_data(img, len);
    if (!img_data)
        g_error("Can't read image data");

    *data = g_memdup(img_data, *len);
    sp_image_release(img);
    return TRUE;
}

static void _track_write_image_to_file(sp_image* img, gpointer data) {
    GError* err = NULL;
    const guchar* img_data = NULL;
    gsize len;
    gchar* filename = (gchar*) data;

    img_data = sp_image_data(img, &len);
    if (!img_data)
        g_error("Can't read image data");
    g_debug("Saving image to %s", filename);
    if (!g_file_set_contents(filename, (gchar*) img_data, len, &err))
        g_error("Can't save image to file: %s", err->message);
    g_free(filename);
    sp_image_release(img);
}

gboolean track_get_image_file(sp_track* track, gchar** filename) {
    if (!filename)
         return FALSE;

    sp_image* img = track_get_image(track);
    if (!img) {
        /* No cover */
        *filename = NULL;
        return FALSE;
    }

    /* Build filename */
    const guchar* img_id = sp_image_image_id(img);
    gchar* b64_id = g_base64_encode(img_id, 20);
    gchar* img_name = g_strdup_printf("%s.jpg", b64_id);
    /* Avoid / in base64-encoded file name! */
    g_strdelimit(img_name, "/", '_');
    *filename = g_build_filename(g_get_user_cache_dir(), g_get_prgname(), img_name, NULL);
    g_free(b64_id);
    g_free(img_name);

    /* If the file already exists, we're done. */
    if (g_file_test(*filename, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR)) {
        sp_image_release(img);
    }

    /* If image is already loaded, write it to the file now */
    else if (sp_image_is_loaded(img)) {
        gchar* fn_copy = g_strdup(*filename);
        _track_write_image_to_file(img, fn_copy);
    }

    /* Load the image and write it with a callback */
    else {
        gchar* fn_copy = g_strdup(*filename);
        sp_image_add_load_callback(img, _track_write_image_to_file, fn_copy);
    }

    return TRUE;
}


/****************
 *** Browsing ***
 ****************/
sp_albumbrowse* albumbrowse_create(sp_album* album, albumbrowse_complete_cb* callback, gpointer userdata) {
    return sp_albumbrowse_create(g_session, album, callback, userdata);
}

sp_artistbrowse* artistbrowse_create(sp_artist* artist, artistbrowse_complete_cb* callback, gpointer userdata) {
    return sp_artistbrowse_create(g_session, artist, SP_ARTISTBROWSE_FULL, callback, userdata);
}

sp_search* search_create(const gchar* query, search_complete_cb* callback, gpointer userdata) {
    int nb_results = config_get_int_opt("search_results", 100);
    return sp_search_create(g_session, query,
                            0, nb_results, 0, nb_results, 0, nb_results, 0, nb_results,
                            SP_SEARCH_STANDARD, callback, userdata);
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
    } while (timeout <= 1);

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
void cb_logged_in(sp_session* session, sp_error error) {
    if (error != SP_ERROR_OK)
        g_warning("Login failed: %s", sp_error_message(error));
    else g_info("Logged in.");

    /* Get the playlists container */
    g_container = sp_session_playlistcontainer(g_session);
    if (!g_container)
        g_error("Could not get the playlist container.");

    /* Then call callbacks */
    session_callback_data scbd;
    scbd.type = SPOP_SESSION_LOGGED_IN;
    scbd.data = NULL;
    g_list_foreach(g_session_callbacks, session_call_callback, &scbd);
}

void cb_logged_out(sp_session* session) {
    g_info("Logged out.");
}
void cb_metadata_updated(sp_session* session) {
}

void cb_connection_error(sp_session* session, sp_error error) {
    g_warning("Connection error: %s", sp_error_message(error));
}
void cb_message_to_user(sp_session* session, const char* message) {
    g_message("%s", message);
}
void cb_notify_main_thread(sp_session* session) {
    g_idle_add_full(G_PRIORITY_DEFAULT, session_libspotify_event, NULL, NULL);
}
int cb_music_delivery(sp_session* session, const sp_audioformat* format, const void* frames, int num_frames) {
    int n = g_audio_delivery_func(format, frames, num_frames);

    if (format->sample_rate == g_audio_rate) {
        g_audio_samples += n;
    }
    else if (n > 0) {
        g_audio_time += (1000 * g_audio_samples) / g_audio_rate;
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
void cb_streaming_error(sp_session* session, sp_error error) {
    g_warning("Streaming error: %s", sp_error_message(error));
}
