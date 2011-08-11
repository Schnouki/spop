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
#include <json-glib/json-glib.h>
#include <libspotify/api.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "spop.h"
#include "commands.h"
#include "queue.h"
#include "spotify.h"
#include "utils.h"

/********************
 *** JSON helpers ***
 ********************/
#define jb_add_bool(jb, name, val) {\
    json_builder_set_member_name(jb, name); \
    json_builder_add_boolean_value(jb, val); }
#define jb_add_int(jb, name, val) {\
    json_builder_set_member_name(jb, name); \
    json_builder_add_int_value(jb, val); }
#define jb_add_string(jb, name, val) {\
    json_builder_set_member_name(jb, name); \
    json_builder_add_string_value(jb, val); }

static void json_tracks_array(GArray* tracks, JsonBuilder* jb) {
    int i;
    sp_track* track;

    bool track_avail;
    int track_duration;
    gchar* track_name;
    gchar* track_artist;
    gchar* track_album;
    gchar* track_link;

    /* For each track, add an object to the JSON array */
    for (i=0; i < tracks->len; i++) {
        track = g_array_index(tracks, sp_track*, i);
        if (!sp_track_is_loaded(track)) continue;

        track_avail = track_available(track);
        track_get_data(track, &track_name, &track_artist, &track_album, &track_link, &track_duration);

        json_builder_begin_object(jb);
        jb_add_string(jb, "artist", track_artist);
        jb_add_string(jb, "title", track_name);
        jb_add_string(jb, "album", track_album);
        jb_add_int(jb, "duration", track_duration);
        jb_add_string(jb, "uri", track_link);
        jb_add_bool(jb, "available", track_avail);
        jb_add_int(jb, "index", i+1);
        json_builder_end_object(jb);

        g_free(track_name);
        g_free(track_artist);
        g_free(track_album);
        g_free(track_link);
    }
}

/****************
 *** Commands ***
 ****************/
void list_playlists(JsonBuilder* jb) {
    int i, n, t;
    sp_playlist* pl;
    sp_playlist_type pt;
    const char* pn;
    gchar* pfn;

    if (!container_loaded()) {
        jb_add_string(jb, "error", "playlists container not loaded yet");
        return;
    }

    n = playlists_len();
    json_builder_set_member_name(jb, "playlists");
    json_builder_begin_array(jb);

    for (i=0; i<n; i++) {
        pt = playlist_type(i);
        switch(pt) {
        case SP_PLAYLIST_TYPE_START_FOLDER:
            g_debug("Playlist %d is a folder start", i);

            json_builder_begin_object(jb);
            pfn = playlist_folder_name(i);
            jb_add_string(jb, "name", pfn);
            g_free(pfn);

            jb_add_string(jb, "type", "folder");

            json_builder_set_member_name(jb, "playlists");
            json_builder_begin_array(jb);
            break;

        case SP_PLAYLIST_TYPE_END_FOLDER:
            g_debug("Playlist %d is a folder end", i);
            json_builder_end_array(jb);
            json_builder_end_object(jb);
            break;

        case SP_PLAYLIST_TYPE_PLAYLIST:
            pl = playlist_get(i);
            json_builder_begin_object(jb);
            if (!pl) {
                g_debug("Got NULL pointer when loading playlist %d.", i);
                json_builder_end_object(jb);
                break;
            }
            if (!sp_playlist_is_loaded(pl)) {
                g_debug("Playlist %d is not loaded.", i);
                json_builder_end_object(jb);
                break;
            }
            pn = sp_playlist_name(pl);

            if (g_strcmp0("-", pn)) {
                /* Regular playlist */
                t = sp_playlist_num_tracks(pl);

                jb_add_string(jb, "type", "playlist");
                jb_add_string(jb, "name", pn);
                jb_add_int(jb, "tracks", t);
                jb_add_int(jb, "index", i);
            }
            else {
                /* Playlist separator */
                jb_add_string(jb, "type", "separator");
            }
            json_builder_end_object(jb);
            break;

        default:
            g_debug("Playlist %d is a placeholder", i);
        }
    }

    json_builder_end_array(jb);
}

void list_tracks(JsonBuilder* jb, int idx) {
    sp_playlist* pl;
    GArray* tracks;

    /* Get the playlist */
    pl = playlist_get(idx);
    if (!pl) {
        jb_add_string(jb, "error", "invalid playlist");
        return;
    }
    
    /* Get the tracks array */
    tracks = tracks_get_playlist(pl);
    if (!tracks) {
        jb_add_string(jb, "error", "playlist not loaded yet");
        return;
    }

    json_builder_set_member_name(jb, "tracks");
    json_builder_begin_array(jb);
    json_tracks_array(tracks, jb);
    json_builder_end_array(jb);

    g_array_free(tracks, TRUE);
}


void status(JsonBuilder* jb) {
    sp_track* track;
    int track_nb, total_tracks, track_duration, track_position;
    queue_status qs;
    gchar* track_name;
    gchar* track_artist;
    gchar* track_album;
    gchar* track_link;
    
    qs = queue_get_status(&track, &track_nb, &total_tracks);

    jb_add_string(jb, "status",
                  (qs == PLAYING) ? "playing"
                  : ((qs == PAUSED) ? "paused" : "stopped"));

    jb_add_bool(jb, "repeat", queue_get_repeat());
    jb_add_bool(jb, "shuffle", queue_get_shuffle());
    jb_add_int(jb, "total_tracks", total_tracks);

    if (qs != STOPPED) {
        jb_add_int(jb, "current_track", track_nb+1);

        track_get_data(track, &track_name, &track_artist, &track_album, &track_link, &track_duration);
        track_position = session_play_time();

        jb_add_string(jb, "artist", track_artist);
        jb_add_string(jb, "title", track_name);
        jb_add_string(jb, "album", track_album);
        jb_add_int(jb, "duration", track_duration);
        jb_add_int(jb, "position", track_position);
        jb_add_string(jb, "uri", track_link);
        g_free(track_name);
        g_free(track_artist);
        g_free(track_album);
        g_free(track_link);
    }
}

void repeat(JsonBuilder* jb) {
    gboolean r = queue_get_repeat();
    queue_set_repeat(TRUE, !r);
    status(jb);
}

void shuffle(JsonBuilder* jb) {
    gboolean s = queue_get_shuffle();
    queue_set_shuffle(TRUE, !s);
    status(jb);
}


void list_queue(JsonBuilder* jb) {
    GArray* tracks;

    tracks = queue_tracks();
    if (!tracks)
        g_error("Couldn't read queue.");

    json_builder_set_member_name(jb, "tracks");
    json_builder_begin_array(jb);
    json_tracks_array(tracks, jb);
    json_builder_end_array(jb);
    g_array_free(tracks, TRUE);
}

void clear_queue(JsonBuilder* jb) {
    queue_clear(TRUE);
    status(jb);
}

void remove_queue_item(JsonBuilder* jb, int idx) {
    remove_queue_items(jb, idx, idx);
}

void remove_queue_items(JsonBuilder* jb, int first, int last) {
    queue_remove_tracks(TRUE, first, last-first+1);
    status(jb);
}


void play_playlist(JsonBuilder* jb, int idx) {
    sp_playlist* pl;

    /* First check the playlist type */
    if (playlist_type(idx) != SP_PLAYLIST_TYPE_PLAYLIST) {
        jb_add_string(jb, "error", "not a playlist");
        return;
    }

    /* Then get the playlist */
    pl = playlist_get(idx);

    if (!pl) {
        jb_add_string(jb, "error", "invalid playlist");
        return;
    }

    /* Load it and play it */
    queue_set_playlist(FALSE, pl);
    queue_play(TRUE);

    status(jb);
}

void play_track(JsonBuilder* jb, int pl_idx, int tr_idx) {
    sp_playlist* pl;
    sp_track* tr;
    GArray* tracks;

    /* First check the playlist type */
    if (playlist_type(pl_idx) != SP_PLAYLIST_TYPE_PLAYLIST) {
        jb_add_string(jb, "error", "not a playlist");
        return;
    }

    /* Then get the playlist */
    pl = playlist_get(pl_idx);
    if (!pl) {
        jb_add_string(jb, "error", "invalid playlist");
        return;
    }

    /* Then get the track itself */
    tracks = tracks_get_playlist(pl);
    if (!tracks) {
        jb_add_string(jb, "error", "playlist not loaded yet");
        return;
    }
    if ((tr_idx <= 0) || (tr_idx > tracks->len)) {
        jb_add_string(jb, "error", "invalid track number");
        g_array_free(tracks, TRUE);
        return;
    }

    tr = g_array_index(tracks, sp_track*, tr_idx-1);
    g_array_free(tracks, TRUE);

    /* Load it and play it */
    queue_set_track(FALSE, tr);
    queue_play(TRUE);

    status(jb);
}

void add_playlist(JsonBuilder* jb, int idx) {
    sp_playlist* pl;
    int tot;

    /* First check the playlist type */
    if (playlist_type(idx) != SP_PLAYLIST_TYPE_PLAYLIST) {
        jb_add_string(jb, "error", "not a playlist");
        return;
    }

    /* Then get the playlist */
    pl = playlist_get(idx);

    if (!pl) {
        jb_add_string(jb, "error", "invalid playlist");
        return;
    }

    /* Load it */
    queue_add_playlist(TRUE, pl);

    queue_get_status(NULL, NULL, &tot);
    jb_add_int(jb, "total_tracks", tot);
}

void add_track(JsonBuilder* jb, int pl_idx, int tr_idx) {
    sp_playlist* pl;
    sp_track* tr;
    GArray* tracks;
    int tot;

    /* First check the playlist type */
    if (playlist_type(pl_idx) != SP_PLAYLIST_TYPE_PLAYLIST) {
        jb_add_string(jb, "error", "not a playlist");
        return;
    }

    /* Then get the playlist */
    pl = playlist_get(pl_idx);
    if (!pl) {
        jb_add_string(jb, "error", "invalid playlist");
        return;
    }

    /* Then get the track itself */
    tracks = tracks_get_playlist(pl);
    if (!tracks) {
        jb_add_string(jb, "error", "playlist not loaded yet");
        return;
    }
    if ((tr_idx <= 0) || (tr_idx > tracks->len)) {
        jb_add_string(jb, "error", "invalid track number");
        g_array_free(tracks, TRUE);
        return;
    }

    tr = g_array_index(tracks, sp_track*, tr_idx-1);

    /* Load it */
    queue_add_track(TRUE, tr);

    queue_get_status(NULL, NULL, &tot);
    jb_add_int(jb, "total_tracks", tot);
}

void play(JsonBuilder* jb) {
    queue_play(TRUE);
    status(jb);
}
void stop(JsonBuilder* jb) {
    queue_stop(TRUE);
    status(jb);
}
void toggle(JsonBuilder* jb) {
    queue_toggle(TRUE);
    status(jb);
}
void seek(JsonBuilder* jb, int pos) {
    queue_seek(pos);
    status(jb);
}

void goto_next(JsonBuilder* jb) {
    queue_next(TRUE);
    status(jb);
}
void goto_prev(JsonBuilder* jb) {
    queue_prev(TRUE);
    status(jb);
}
void goto_nb(JsonBuilder* jb, int nb) {
    queue_goto(TRUE, nb-1, TRUE);
    status(jb);
}
