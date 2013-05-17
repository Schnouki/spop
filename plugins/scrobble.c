/*
 * Copyright (C) 2010, 2011, 2012, 2013 Thomas Jost
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
#include <glib.h>
#include <gmodule.h>
#include <libsoup/soup.h>
#include <libspotify/api.h>
#include <string.h>
#include <time.h>

#include "spop.h"
#include "config.h"
#include "spotify.h"

#define SCROBBLE_CLIENT_ID      "spp"
#define SCROBBLE_CLIENT_VERSION SPOP_VERSION "a"

/* Global variables */
static SoupSession* g_session = NULL;
static gchar* g_token = NULL;
static gboolean g_token_requested = FALSE;
static gchar* g_nowplaying_url = NULL;
static gchar* g_scrobble_url = NULL;

static GList* g_tracks = NULL;
typedef struct {
    gchar* artist;
    gchar* track;
    gchar* album;
    int length;
    gboolean np_submitted;
    gboolean np_submitting;
    gboolean scrobbled;
    gboolean scrobbling;
    time_t start;
    int play_time;
} track_data;

/* Functions prototypes */
static void session_callback(session_callback_type type, gpointer data, gpointer user_data);

static void now_playing_request(sp_track* track);
static gboolean now_playing_handler(gpointer data);
static void now_playing_callback(SoupSession* session, SoupMessage* msg, gpointer user_data);

static void scrobble_request();
static gboolean scrobble_handler(gpointer data);
static void scrobble_callback(SoupSession* session, SoupMessage* msg, gpointer user_data);

static void token_request();
static void token_callback(SoupSession* session, SoupMessage* msg, gpointer user_data);

static int min(int a, int b) { return a>b ? b : a; }


/************************************
 * Spop session callback management *
 ************************************/
static void session_callback(session_callback_type type, gpointer data, gpointer user_data) {
    GList* cur;
    GList* next;
    track_data* td;
    gboolean first;

    /* First do some cleanup.
       - First item in the list: clean if both submitted and scrobbled
       - Other items: clean if scrobbled (it won't be submitted anyway...)
       - All of them: don't clean if the submission is in progress
    */
    cur = g_tracks;
    first = TRUE;
    while (cur != NULL) {
        next = cur->next;
        td = cur->data;

        if (td->scrobbled && !td->scrobbling && !td->np_submitting && ((first && td->np_submitted) || !first)) {
            /* This item is not needed anymore */
            g_debug("scrobble: Cleaning an item: \"%s - %s\".", td->artist, td->track);
            g_free(td->artist);
            g_free(td->track);
            g_free(td->album);
            g_tracks = g_list_delete_link(g_tracks, cur);
        }
        first = FALSE;
        cur = next;
    }

    /* Then handle the session event */
    if (type == SPOP_SESSION_LOAD)
        now_playing_request((sp_track*) data);
    else if (type == SPOP_SESSION_UNLOAD)
        scrobble_request();
}

/***************************************
 * "Now playing" submission management *
 ***************************************/
static void now_playing_request(sp_track* track) {
    track_data* td = NULL;

    g_debug("scrobble: Preparing a \"now playing\" request.");

    /* Get some informations about the current track */
    td = g_malloc(sizeof(track_data));
    guint length_ms;
    track_get_data(track, &td->track, &td->artist, &td->album, NULL, &length_ms, NULL);

    if (!td->artist) td->artist = g_strdup("");
    if (!td->track)  td->track  = g_strdup("");
    if (!td->album)  td->album  = g_strdup("");
    td->length = length_ms / 1000;

    td->np_submitted = FALSE;
    td->np_submitting = FALSE;
    td->scrobbled = FALSE;
    td->scrobbling = FALSE;

    td->start = time(NULL);
    if (td->start == -1)
        g_error("scrobble: Can't get current time: %s", g_strerror(errno));

    td->play_time = -1;

    /* Add these data to the list */
    g_tracks = g_list_prepend(g_tracks, td);

    /* Try to submit the request; if it fails; try again in one second */
    if (now_playing_handler(NULL))
        g_timeout_add_seconds(1, now_playing_handler, NULL);
}

static gboolean now_playing_handler(gpointer data) {
    SoupMessage* message;
    track_data* td;
    gchar* len;

    g_debug("scrobble: Entering the \"now playing\" handler.");

    /* Is there a valid session token? */
    if (!g_token) {
        if (!g_token_requested)
            token_request();
        return TRUE;
    }

    /* Get the latest track */
    if (!g_tracks) {
        g_warning("scrobble: No data for the current track.");
        return FALSE;
    }
    td = g_tracks->data;

    /* Was it already submitted, or is the submission in progress? */
    if (td->np_submitted || td->np_submitting)
        return FALSE;

    /* Prepare the message and queue it */
    len = g_strdup_printf("%u", td->length);
    g_debug("scrobble: Sending \"Now playing\" request for \"%s - %s\"", td->artist, td->track);
    message = soup_form_request_new("POST", g_nowplaying_url,
                                    "s", g_token,
                                    "a", td->artist,
                                    "t", td->track,
                                    "b", td->album,
                                    "l", len,
                                    "n", "",
                                    "m", "",
                                    NULL);
    g_free(len);
    soup_session_queue_message(g_session, message, now_playing_callback, td);
    td->np_submitting = TRUE;

    return FALSE;
}

static void now_playing_callback(SoupSession* session, SoupMessage* msg, gpointer user_data) {
    track_data* td = user_data;

    td->np_submitting = FALSE;

    /* Success? */
    if (msg->status_code != 200) {
        g_info("scrobble: \"Now playing\" request ended with status code %d.", msg->status_code);
    }
    else if (strncmp(msg->response_body->data, "OK", 2) != 0) {
        gchar* str = g_strndup(msg->response_body->data, msg->response_body->length);
        g_info("scrobble: \"Now playing\" request failed: %s", str);
        g_free(str);
    }
    else {
        /* Success: mark the track as submitted */
        g_debug("scrobble: Marking track \"%s - %s\" as submitted.", td->artist, td->track);
        td->np_submitted = TRUE;
        return;
    }

    /* If we reach this point, there was an error: try again in one second */
    g_free(g_token);
    g_token = NULL;
    g_timeout_add_seconds(1, now_playing_handler, NULL);
}

/*************************
 * Scrobbling management *
 *************************/
static void scrobble_request() {
    track_data* td = NULL;

    g_debug("scrobble: Preparring a scrobbling request.");

    if (!g_tracks) {
        g_warning("scrobble: No data for the current track.");
        return;
    }
    td = g_tracks->data;

    td->play_time = session_play_time() / 1000;
    /* Try to scrobble this. If it fails, try again in a second */
    if (scrobble_handler(NULL))
        g_timeout_add_seconds(1, scrobble_handler, NULL);
}

static gboolean scrobble_handler(gpointer data) {
    GList* cur;
    GList* scrobbles;
    GHashTable* h;
    int i;
    SoupMessage* message;
    track_data* td;

    g_debug("scrobble: Entering the scrobbling handler.");

    /* Is there a valid session token? */
    if (!g_token) {
        if (!g_token_requested)
            token_request();
        return TRUE;
    }

    /* Hash table used to store key/values for the scrobbling request */
    h = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    if (!h)
        g_error("scrobble: Can't create a new hash table.");

    /* Fill the hash table with values from g_tracks */
    cur = g_list_last(g_tracks);
    scrobbles = NULL;
    i = 0;
    while (cur != NULL) {
        td = cur->data;
        cur = cur->prev;

        /* Should this track be scrobbled?
           - longer than 30 seconds
           - played for at least 240 seconds, or half of the track, whichever
             comes first
           - not scrobbled yet */
        if (td->scrobbled || td->scrobbling) {
            continue;
        }
        else if ((td->length >= 30) && (td->play_time >= min(240, td->length/2))) {
            /* Scrobble! */
            g_debug("scrobble: Scrobbling \"%s - %s\".", td->artist, td->track);
            scrobbles = g_list_prepend(scrobbles, td);
            td->scrobbling = TRUE;

            g_hash_table_insert(h,
                                g_strdup_printf("a[%d]", i),
                                g_strdup(td->artist));
            g_hash_table_insert(h,
                                g_strdup_printf("t[%d]", i),
                                g_strdup(td->track));
            g_hash_table_insert(h,
                                g_strdup_printf("i[%d]", i),
                                g_strdup_printf("%ld", td->start));
            g_hash_table_insert(h,
                                g_strdup_printf("o[%d]", i),
                                g_strdup("P"));
            g_hash_table_insert(h,
                                g_strdup_printf("r[%d]", i),
                                g_strdup(""));
            g_hash_table_insert(h,
                                g_strdup_printf("l[%d]", i),
                                g_strdup_printf("%d", td->length));
            g_hash_table_insert(h,
                                g_strdup_printf("b[%d]", i),
                                g_strdup(td->album));
            g_hash_table_insert(h,
                                g_strdup_printf("n[%d]", i),
                                g_strdup(""));
            g_hash_table_insert(h,
                                g_strdup_printf("m[%d]", i),
                                g_strdup(""));

            i += 1;
        }
        else {
            /* Don't scrobble. Mark as scrobbled so that it can be freed. */
            td->scrobbled = TRUE;
        }
    }

    if (i > 0) {
        /* There is something to scrobble: prepare and send the message */
        g_hash_table_insert(h, g_strdup("s"), g_strdup(g_token));
        message = soup_form_request_new_from_hash("POST", g_scrobble_url, h);
        soup_session_queue_message(g_session, message, scrobble_callback, scrobbles);
    }

    /* Cleanup */
    g_hash_table_destroy(h);

    return FALSE;
}

static void scrobble_callback(SoupSession* session, SoupMessage* msg, gpointer user_data) {
    GList* scrobbles = user_data;
    GList* cur;
    track_data* td;

    /* Remove the scrobbling flag */
    cur = scrobbles;
    while (cur != NULL) {
        td = cur->data;
        td->scrobbling = FALSE;
        cur = cur->next;
    }

    /* Success? */
    if (msg->status_code != 200) {
        g_info("scrobble: Scrobbling request ended with status code %d.", msg->status_code);
    }
    else if (strncmp(msg->response_body->data, "OK", 2) != 0) {
        gchar* str = g_strndup(msg->response_body->data, msg->response_body->length);
        g_info("scrobble: Scrobbling request failed: %s", str);
        g_free(str);
    }
    else {
        /* Success: mark all the tracks as scrobbled */
        int n=0;
        cur = scrobbles;
        while (cur != NULL) {
            td = cur->data;
            g_debug("scrobble: Marking track \"%s - %s\" as scrobbled.", td->artist, td->track);
            td->scrobbled = TRUE;
            cur = cur->next;
            n += 1;
        }
        return;
    }

    /* If we reach this point, there was an error: try again in one second */
    g_free(g_token);
    g_token = NULL;
    g_timeout_add_seconds(1, scrobble_handler, NULL);
}


/****************************
 * Session token management *
 ****************************/
/* Create and send a token request to the handshake server */
static void token_request() {
    GString* auth_token;
    gchar* auth_token_md5;
    gchar* handshake_url;
    gchar* password;
    gchar* password_md5;
    gchar* username;
    gchar* timestamp;
    time_t t;

    SoupMessage* message;
    SoupURI* uri;

    if (g_token || g_token_requested)
        return;

    /* Compute the authentication token.
       token = md5(md5(password) + timestamp)
       (http://www.lastfm.fr/api/submissions#1.2 for details) */
    password = config_get_string_group("scrobble", "password");
    password_md5 = g_compute_checksum_for_string(G_CHECKSUM_MD5, password, -1);
    auth_token = g_string_new(password_md5);
    g_free(password_md5);

    t = time(NULL);
    if (t == -1)
        g_error("scrobble: Can't get current time: %s", g_strerror(errno));

    timestamp = g_strdup_printf("%ld", t);

    g_string_append(auth_token, timestamp);
    auth_token_md5 = g_compute_checksum_for_string(G_CHECKSUM_MD5, auth_token->str, -1);
    g_debug("scrobble: auth_token is %s, (timestamp: %s, md5: %s)",
            auth_token->str, timestamp, auth_token_md5);
    g_string_free(auth_token, TRUE);

    /* Prepare the URI */
    handshake_url = config_get_string_group("scrobble", "api_endpoint");
    username = config_get_string_group("scrobble", "username");

    uri = soup_uri_new(handshake_url);
    if (!uri)
        g_error("scrobble: Can't create URI.");

    soup_uri_set_query_from_fields(uri,
                                   "hs", "true", /* handshake */
                                   "p", "1.2.1", /* protocol version */
                                   "c", SCROBBLE_CLIENT_ID,
                                   "v", SCROBBLE_CLIENT_VERSION,
                                   "u", username,
                                   "t", timestamp,
                                   "a", auth_token_md5,
                                   NULL);
    if (debug_mode) {
        gchar* uri_string = soup_uri_to_string(uri, FALSE);
        g_debug("scrobble: handshake URI: %s", uri_string);
        g_free(uri_string);
    }
    g_free(timestamp);
    g_free(auth_token_md5);

    /* Prepare the message and queue it*/
    message = soup_message_new_from_uri("GET", uri);
    soup_session_queue_message(g_session, message, token_callback, NULL);
    g_token_requested = TRUE;

    /* Cleanup */
    soup_uri_free(uri);
}

/* Callback to a token request */
static void token_callback(SoupSession* session, SoupMessage* msg, gpointer user_data) {
    const gchar* bol;
    const gchar* eol;
    gssize len;

    /* Success? */
    if (msg->status_code != 200) {
        g_info("scrobble: Token request ended with status code %d.", msg->status_code);
        goto token_fail;
    }

    /* Read the first line: OK/BANNED/BADAUTH/BADTIME/FAILED... */
    bol = msg->response_body->data;
    len = msg->response_body->length;
    eol = g_strstr_len(bol, len, "\n");
    if (!eol) {
        g_warning("scrobble: Can't parse 1st line of token response.");
        goto token_fail;
    }
    if (strncmp(bol, "OK", eol-bol) != 0) {
        gchar* msg = g_strndup(bol, eol-bol);
        g_info("scrobble: Token request failed: %s", msg);
        g_free(msg);
        goto token_fail;
    }

    /* Read the second line: session token */
    len -= (eol-bol) + 1;
    bol = eol+1;
    eol = g_strstr_len(bol, len, "\n");
    if (!eol) {
        g_warning("scrobble: Can't parse 2nd line of token response.");
        goto token_fail;
    }
    if (g_token)
        g_free(g_token);
    g_token = g_strndup(bol, eol-bol);

    /* Read the third line: "Now Playing" URL */
    len -= (eol-bol) + 1;
    bol = eol+1;
    eol = g_strstr_len(bol, len, "\n");
    if (!eol) {
        g_warning("scrobble: Can't parse 3rd line of token response.");
        goto token_fail;
    }
    if (g_nowplaying_url)
        g_free(g_nowplaying_url);
    g_nowplaying_url = g_strndup(bol, eol-bol);

    /* Read the fourth line: scrobble URL */
    len -= (eol-bol) + 1;
    bol = eol+1;
    eol = g_strstr_len(bol, len, "\n");
    if (!eol) {
        g_warning("scrobble: Can't parse 4th line of token response.");
        goto token_fail;
    }
    if (g_scrobble_url)
        g_free(g_scrobble_url);
    g_scrobble_url = g_strndup(bol, eol-bol);

    g_token_requested = FALSE;

    g_debug("scrobble: auth token: %s, \"now playing\" URL: %s, scrobble URL: %s",
            g_token, g_nowplaying_url, g_scrobble_url);
    return;

token_fail:
    if (g_token)
        g_free(g_token);
    g_token = NULL;
    g_token_requested = FALSE;

    if (g_nowplaying_url)
        g_free(g_nowplaying_url);
    g_nowplaying_url = NULL;

    if (g_scrobble_url)
        g_free(g_scrobble_url);
    g_scrobble_url = NULL;
}


/*************************
 * Plugin initialization *
 *************************/
G_MODULE_EXPORT void spop_scrobble_init() {
    /* Init libsoup */
    g_session = soup_session_async_new();

    if (debug_mode) {
        SoupLogger* logger = soup_logger_new(SOUP_LOGGER_LOG_BODY, -1);
        soup_session_add_feature(g_session, SOUP_SESSION_FEATURE(logger));
        g_object_unref(logger);
    }

    /* Register the session callback */
    if (!session_add_callback(session_callback, NULL))
        g_error("Could not add scrobble callback.");

    /* Request a session token */
    token_request();
}

G_MODULE_EXPORT void spop_scrobble_close() {
}
