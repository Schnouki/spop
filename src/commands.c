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

/*******************************
 *** Arguments types helpers ***
 *******************************/
#define spop_command(cmd_name) \
    void cmd_name(JsonBuilder* jb)

#define spop_command__int(cmd_name, arg1)               \
    static void _##cmd_name(JsonBuilder* jb, int arg1); \
    void cmd_name(JsonBuilder* jb, const gchar* sarg1) {                \
        gchar* endptr;                                                  \
        int arg1 = strtol(sarg1, &endptr, 0);                           \
        if ((endptr == sarg1) || (arg1 < 0)) {                          \
            g_debug("Invalid argument: %s", sarg1);                     \
            jb_add_string(jb, "error", "invalid argument 1 (should be an integer)"); \
            return;                                                     \
        }                                                               \
        _##cmd_name(jb, arg1);                                          \
    }                                                                   \
    static void _##cmd_name(JsonBuilder* jb, int arg1)

#define spop_command__int_int(cmd_name, arg1, arg2)     \
    static void _##cmd_name(JsonBuilder* jb, int arg1, int arg2);       \
    void cmd_name(JsonBuilder* jb, const gchar* sarg1, const gchar* sarg2) { \
        gchar* endptr;                                                  \
        int arg1 = strtol(sarg1, &endptr, 0);                           \
        if ((endptr == sarg1) || (arg1 < 0)) {                          \
            g_debug("Invalid argument: %s", sarg1);                     \
            jb_add_string(jb, "error", "invalid argument 1 (should be an integer)"); \
            return;                                                     \
        }                                                               \
        int arg2 = strtol(sarg2, &endptr, 0);                           \
        if ((endptr == sarg2) || (arg2 < 0)) {                          \
            g_debug("Invalid argument: %s", sarg2);                     \
            jb_add_string(jb, "error", "invalid argument 2 (should be an integer)"); \
            return;                                                     \
        }                                                               \
        _##cmd_name(jb, arg1, arg2);                                    \
    }                                                                   \
    static void _##cmd_name(JsonBuilder* jb, int arg1, int arg2)

/****************
 *** Commands ***
 ****************/
spop_command(list_playlists) {
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

spop_command__int(list_tracks, idx) {
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


spop_command(status) {
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

spop_command(repeat) {
    gboolean r = queue_get_repeat();
    queue_set_repeat(TRUE, !r);
    status(jb);
}

spop_command(shuffle) {
    gboolean s = queue_get_shuffle();
    queue_set_shuffle(TRUE, !s);
    status(jb);
}


spop_command(list_queue) {
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

spop_command(clear_queue) {
    queue_clear(TRUE);
    status(jb);
}

spop_command__int_int(remove_queue_items, first, last) {
    queue_remove_tracks(TRUE, first, last-first+1);
    status(jb);
}

spop_command__int(remove_queue_item, idx) {
    _remove_queue_items(jb, idx, idx);
}

spop_command__int(play_playlist, idx) {
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

spop_command__int_int(play_track, pl_idx, tr_idx) {
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

spop_command__int(add_playlist, idx) {
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

spop_command__int_int(add_track, pl_idx, tr_idx) {
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

spop_command(play) {
    queue_play(TRUE);
    status(jb);
}
spop_command(stop) {
    queue_stop(TRUE);
    status(jb);
}
spop_command(toggle) {
    queue_toggle(TRUE);
    status(jb);
}
spop_command__int(seek, pos) {
    queue_seek(pos);
    status(jb);
}

spop_command(goto_next) {
    queue_next(TRUE);
    status(jb);
}
spop_command(goto_prev) {
    queue_prev(TRUE);
    status(jb);
}
spop_command__int(goto_nb, nb) {
    queue_goto(TRUE, nb-1, TRUE);
    status(jb);
}

spop_command(image) {
    sp_track* track = NULL;
    guchar* img_data;
    gsize len;
    gboolean res;

    queue_get_status(&track, NULL, NULL);
    res = track_get_image_data(track, (gpointer*) &img_data, &len);
    if (!res) {
        jb_add_string(jb, "status", "not-loaded");
    }
    else if (!img_data) {
        jb_add_string(jb, "status", "absent");
    }
    else {
        gchar* b64data = g_base64_encode(img_data, len);
        jb_add_string(jb, "status", "ok");
        jb_add_string(jb, "data", b64data);
        
        g_free(b64data);
        g_free(img_data);
    }
}
