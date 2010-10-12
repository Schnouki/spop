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

#include <glib.h>
#include <gmodule.h>
#include <libsoup/soup.h>
#include <libspotify/api.h>
#include <string.h>

#include "spop.h"
#include "config.h"
#include "spotify.h"

#define SCROBBLE_CLIENT_ID      "tst"
#define SCROBBLE_CLIENT_VERSION "1.0"

/* Global variables */
static SoupSession* g_session = NULL;
static gchar* g_token = NULL;
static gboolean g_token_requested = FALSE;
static gchar* g_nowplaying_url = NULL;
static gchar* g_scrobble_url = NULL;

/* Functions prototypes */
static void session_callback(session_callback_type type, gpointer data, gpointer user_data);

static void now_playing_request(sp_track* track);
static gboolean now_playing_handler(gpointer data);
static void now_playing_callback(SoupSession* session, SoupMessage* msg, gpointer user_data);

static void scrobble_request();
//static void scrobble_callback();

static void token_request();
static void token_callback(SoupSession* session, SoupMessage* msg, gpointer user_data);

typedef struct {
    gchar* artist;
    gchar* track;
    gchar* album;
    int length;
} now_playing_request_data;


/************************************
 * Spop session callback management *
 ************************************/
static void session_callback(session_callback_type type, gpointer data, gpointer user_data) {
    if (type == SPOP_SESSION_LOAD)
        now_playing_request((sp_track*) data);
    else if (type == SPOP_SESSION_UNLOAD)
        scrobble_request();
}

/***************************************
 * "Now playing" submission management *
 ***************************************/
static void now_playing_request(sp_track* track) {
    now_playing_request_data* nprd = NULL;
    int min, sec;

    g_debug("scrobble: Preparing a \"now playing\" request.");

    /* Prepare the data structure to pass to the event handler */
    nprd = g_malloc(sizeof(now_playing_request_data));
    track_get_data(track, &nprd->track, &nprd->artist, &nprd->album, NULL, &min, &sec);
    nprd->length = min*60 + sec;

    /* Try to submit the request; if it fails; try again in one second */
    if (now_playing_handler(nprd))
        g_timeout_add_seconds(1, now_playing_handler, nprd);
}

static gboolean now_playing_handler(gpointer data) {
    now_playing_request_data* nprd = data;
    GString* len;

    SoupMessage* message;

    g_debug("scrobble: Entering the \"now playing\" handler.");

    /* Is there a valid session token? */
    if (!g_token) {
        if (!g_token_requested)
            token_request();
        return TRUE;
    }

    /* Validate data */
    len = g_string_sized_new(5);
    g_string_printf(len, "%d", nprd->length);

    if (!nprd->artist) nprd->artist = g_strdup("");
    if (!nprd->track)  nprd->track  = g_strdup("");
    if (!nprd->album)  nprd->album  = g_strdup("");

    /* Prepare the message and queue it */
    message = soup_form_request_new("POST", g_nowplaying_url,
                                    "s", g_token,
                                    "a", nprd->artist,
                                    "t", nprd->track,
                                    "b", nprd->album,
                                    "l", len->str,
                                    "n", "",
                                    "m", "",
                                    NULL);
    g_string_free(len, TRUE);
    soup_session_queue_message(g_session, message, now_playing_callback, nprd);

    return FALSE;
}

static void now_playing_callback(SoupSession* session, SoupMessage* msg, gpointer user_data) {
    now_playing_request_data* nprd = user_data;

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
        /* Success: do some cleanup */
        g_free(nprd->artist);
        g_free(nprd->track);
        g_free(nprd->album);
        g_free(nprd);
        return;
    }

    /* If we reach this point, there was an error: try again in one second */
    g_free(g_token);
    g_token = NULL;
    g_timeout_add_seconds(1, now_playing_handler, nprd);
}

/*************************
 * Scrobbling management *
 *************************/
static void scrobble_request() {
}

/****************************
 * Session token management *
 ****************************/
/* Create and send a token request to the handshake server */
static void token_request() {
    GString* auth_token;
    gchar* auth_token_md5;
    GDateTime* dt;
    gchar* handshake_url;
    gchar* password;
    gchar* password_md5;
    gchar* username;
    gchar* timestamp;

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
    
    dt = g_date_time_new_now_local();
    if (!dt)
        g_error("scrobble: Can't get current date/time.");

    timestamp = g_date_time_format(dt, "%s");
    if (!timestamp)
        g_error("scrobble: Could not convert date/time to a timestamp.");
    g_date_time_unref(dt);

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
