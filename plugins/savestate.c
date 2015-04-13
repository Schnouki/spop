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

#include <glib.h>
#include <gmodule.h>
#include <json-glib/json-glib.h>
#include <libspotify/api.h>
#include <string.h>

#include "spop.h"
#include "interface.h"
#include "queue.h"
#include "spotify.h"

/* Global variables */
static gchar* g_state_file_path = NULL;

typedef struct {
    queue_status qs;
    gboolean repeat, shuffle;
    int cur_track;
    GArray* tracks;
} saved_state;

/* Save state */
static gboolean save_state(gpointer data) {
    queue_status qs;
    gboolean repeat, shuffle;
    int cur_track;
    GArray* tracks = NULL;

    JsonBuilder* jb = NULL;
    JsonGenerator* gen = NULL;
    gchar* out = NULL;

    GError* err = NULL;

    g_debug("savestate: starting to save the current state...");

    /* Get data about the current state */
    qs = queue_get_status(NULL, &cur_track, NULL);
    tracks = queue_tracks();
    repeat = queue_get_repeat();
    shuffle = queue_get_shuffle();

    /* Save them in JSON */
    jb = json_builder_new();
    json_builder_begin_object(jb);

    json_builder_set_member_name(jb, "status");
    switch (qs) {
    case STOPPED:
        json_builder_add_string_value(jb, "stopped"); break;
    case PLAYING:
        json_builder_add_string_value(jb, "playing"); break;
    case PAUSED:
        json_builder_add_string_value(jb, "paused"); break;
    default:
        g_warning("savestate: bad queue_status value: %d", qs);
        goto savestate_clean;
    }

    json_builder_set_member_name(jb, "repeat");
    json_builder_add_boolean_value(jb, repeat);

    json_builder_set_member_name(jb, "shuffle");
    json_builder_add_boolean_value(jb, shuffle);

    json_builder_set_member_name(jb, "current_track");
    json_builder_add_int_value(jb, cur_track);

    json_builder_set_member_name(jb, "tracks");
    json_builder_begin_array(jb);

    int i;
    for (i=0; i < tracks->len; i++) {
        sp_track* tr = g_array_index(tracks, sp_track*, i);
        if (!sp_track_is_loaded(tr)) {
            g_warning("savestate: queue track %d is not loaded", i);
            goto savestate_clean;
        }

        sp_link* lnk = sp_link_create_from_track(tr, 0);
        gchar uri[1024];
        int uri_len = sp_link_as_string(lnk, uri, 1024);
        sp_link_release(lnk);
        if (uri_len >= 1024) {
            g_warning("savestate: URI too long for track %d", i);
            goto savestate_clean;
        }

        json_builder_add_string_value(jb, uri);
    }
    json_builder_end_array(jb);
    json_builder_end_object(jb);

    /* Store JSON to file */
    gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(jb));
    out = json_generator_to_data(gen, NULL);

    if (g_file_set_contents(g_state_file_path, out, -1, &err))
        g_debug("savestate: state saved to %s.", g_state_file_path);
    else
        g_warning("savestate: unable to dump status to file: %s", err->message);

 savestate_clean:
    if (tracks)
        g_array_free(tracks, TRUE);
    if (gen)
        g_object_unref(gen);
    if (jb)
        g_object_unref(jb);
    if (out)
        g_free(out);
    return FALSE;
}

/* Notification callback */
static void savestate_notification_callback(const GString* status, gpointer data) {
    /* Schedule a call to save_state when idle. This is added with priority
       G_PRIORITY_DEFAULT_IDLE, which is lower than G_PRIORITY_DEFAULT, so this
       should not interfere with anything else. */
    guint ev = g_idle_add(save_state, NULL);
    g_debug("savestate: idle callback added (%d)", ev);
}

/* Restore state */
static gboolean really_restore_state(gpointer data) {
    saved_state* s = (saved_state*) data;
    sp_track* tr;

    /* Check if all tracks are loaded */
    size_t i;
    for (i=0; i < s->tracks->len; i++) {
        tr = g_array_index(s->tracks, sp_track*, i);
        if (!sp_track_is_loaded(tr))
            return TRUE;
    }

    /* All tracks are loaded: restore them */
    g_debug("savestate: restoring saved state...");
    queue_clear(FALSE);

    for (i=0; i < s->tracks->len; i++) {
        tr = g_array_index(s->tracks, sp_track*, i);
        if (track_available(tr))
            queue_add_track(FALSE, tr);
        else {
            g_info("savestate: track %zu is no longer available", i);
            if (s->cur_track == i) {
                s->cur_track = -1;
                s->qs = STOPPED;
            }
            else if (s->cur_track > i)
                s->cur_track -= 1;
        }
        sp_track_release(tr);
    }

    queue_set_repeat(FALSE, s->repeat);
    queue_set_shuffle(FALSE, s->shuffle);

    if (s->cur_track >= 0)
        queue_goto(FALSE, s->cur_track, TRUE);
    if (s->qs == PLAYING)
        queue_play(FALSE);

    /* Done! */
    queue_notify();

    g_array_free(s->tracks, TRUE);
    g_free(s);

    g_debug("savestate: state restored!");

    return FALSE;
}

static void restore_state(session_callback_type type, gpointer data, gpointer user_data) {
    JsonParser* jp = NULL;
    JsonReader* jr = NULL;

    const gchar* sqs;
    saved_state* s = NULL;

    GError* err = NULL;

    /* Is it the callback we're interested in? */
    if (type != SPOP_SESSION_LOGGED_IN)
        return;

    /* First disable the callback so it's not called again */
    session_remove_callback(restore_state, NULL);

    g_debug("savestate: reading saved state...");
    s = g_new0(saved_state, 1);

    /* Read and parse state file */
    jp = json_parser_new();
    if (!json_parser_load_from_file(jp, g_state_file_path, &err)) {
        g_warning("savestate: error while reading state file: %s", err->message);
        goto restorestate_error;
    }

    jr = json_reader_new(json_parser_get_root(jp));

    /* Read basic state */
    if (!json_reader_read_member(jr, "status")) goto restorestate_jr_error;
    sqs = json_reader_get_string_value(jr);
    json_reader_end_member(jr);
    if      (strcmp(sqs, "stopped")) s->qs = STOPPED;
    else if (strcmp(sqs, "playing")) s->qs = PLAYING;
    else if (strcmp(sqs, "paused"))  s->qs = PAUSED;
    else {
        g_warning("savestate: bad value for queue status: %s", sqs);
        goto restorestate_error;
    }

    if (!json_reader_read_member(jr, "repeat")) goto restorestate_jr_error;
    s->repeat = json_reader_get_boolean_value(jr);
    json_reader_end_member(jr);

    if (!json_reader_read_member(jr, "shuffle")) goto restorestate_jr_error;
    s->shuffle = json_reader_get_boolean_value(jr);
    json_reader_end_member(jr);

    if (!json_reader_read_member(jr, "current_track")) goto restorestate_jr_error;
    s->cur_track = json_reader_get_int_value(jr);
    json_reader_end_member(jr);

    /* Now read tracks URIs */
    if (!json_reader_read_member(jr, "tracks")) goto restorestate_jr_error;
    if (!json_reader_is_array(jr)) {
        g_warning("savestate: error while parsing JSON: tracks is not an array");
        goto restorestate_error;
    }
    gint tracks = json_reader_count_elements(jr);
    if (s->cur_track >= tracks) {
        g_warning("savestate: incoherent state file: cur_track >= tracks");
        goto restorestate_error;
    }

    s->tracks = g_array_sized_new(FALSE, FALSE, sizeof(sp_track*), tracks);
    if (!s->tracks)
        g_error("Can't allocate array of %d tracks.", tracks);

    size_t i;
    gboolean can_restore_now = TRUE;
    for (i=0; i < tracks; i++) {
        json_reader_read_element(jr, i);
        const gchar* uri = json_reader_get_string_value(jr);
        json_reader_end_element(jr);

        sp_link* lnk = sp_link_create_from_string(uri);
        sp_linktype lt = sp_link_type(lnk);
        if (lt != SP_LINKTYPE_TRACK) {
            g_warning("savestate: invalid link type for track %zu: %d", i, lt);
            sp_link_release(lnk);
            goto restorestate_error;
        }
        sp_track* tr = sp_link_as_track(lnk);
        sp_track_add_ref(tr);
        sp_link_release(lnk);
        g_array_append_val(s->tracks, tr);
        if (!sp_track_is_loaded(tr))
            can_restore_now = FALSE;
    }

    /* If possible, restore now, else wait for all tracks to be loaded */
    if (can_restore_now)
        really_restore_state(s);
    else {
        g_timeout_add(100, (GSourceFunc) really_restore_state, s);
        g_debug("savestate: waiting for all tracks to be loaded before restoring saved state...");
    }

    /* Add a notification callback */
    if (!interface_notify_add_callback(savestate_notification_callback, NULL))
        g_error("Could not add savestate callback.");

    goto restorestate_clean;

 restorestate_jr_error:
    err = (GError*) json_reader_get_error(jr);
    g_warning("savestate: error while parsing JSON: %s", err->message);

 restorestate_error:
    if (s) {
        if (s->tracks)
            g_array_free(s->tracks, TRUE);
        g_free(s);
    }

 restorestate_clean:
    if (jp)
        g_object_unref(jp);
    if (jr)
        g_object_unref(jr);
}

/* Plugin management */
G_MODULE_EXPORT void spop_savestate_init() {
    g_state_file_path = g_build_filename(g_get_user_cache_dir(), g_get_prgname(), "state.json", NULL);

    /* Startup time: restore the previous state once we're logged in */
    session_add_callback(restore_state, NULL);
}

G_MODULE_EXPORT void spop_savestate_close() {
    /* Exit time: save the current state */
    save_state(NULL);

    g_free(g_state_file_path);
}
