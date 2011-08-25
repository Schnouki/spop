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
#include "config.h"
#include "interface.h"
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

/***************************
 *** Commands management ***
 ***************************/
/* Run the given command with the given arguments */
gboolean command_run(GIOChannel* chan, command_descriptor* desc, int argc, char** argv) {
    gboolean ret = TRUE;
    command_context* ctx = g_new(command_context, 1);
    ctx->chan = chan;
    ctx->jb = json_builder_new();
    json_builder_begin_object(ctx->jb);

#define _str_to_uint(dst, src)                  \
    guint dst; {                                \
    gchar* endptr;                              \
    dst = strtoul(src, &endptr, 0);             \
    if (endptr == src) {                        \
        g_debug("Invalid argument: %s", src);   \
        jb_add_string(ctx->jb, "error", "invalid argument (should be an unsigned integer)"); \
        goto cr_end;                            \
    }}
#define _str_to_link(dst, src)                  \
    sp_link* dst; {                             \
    dst = sp_link_create_from_string(src);      \
    if (!dst) {                                 \
        g_debug("Invalid argument: %s", src);   \
        jb_add_string(ctx->jb, "error", "invalid argument (should be a Spotify URI)"); \
        goto cr_end;                                                    \
    }}

    if (desc->args[0] == CA_NONE) {
        gboolean (*cmd)(command_context*) = desc->func;
        ret = cmd(ctx);
    }
    else if (desc->args[0] == CA_INT) {
        _str_to_uint(arg1, argv[1]);
        if (desc->args[1] == CA_NONE) {
            gboolean (*cmd)(command_context*, guint) = desc->func;
            ret = cmd(ctx, arg1);
        }
        else if (desc->args[1] == CA_INT) {
            _str_to_uint(arg2, argv[2]);
            gboolean (*cmd)(command_context*, guint, guint) = desc->func;
            ret = cmd(ctx, arg1, arg2);
        }
        else
            g_error("Unknown argument type");
    }
    else if (desc->args[0] == CA_URI) {
        _str_to_link(arg1, argv[1]);
        if (desc->args[1] == CA_NONE) {
            gboolean (*cmd)(command_context*, sp_link*) = desc->func;
            ret = cmd(ctx, arg1);
        }
        else
            g_error("Unknown argument type");
    }
    else
        g_error("Unknown argument type");

 cr_end:
    if (ret)
        command_end(ctx);
    return ret;
}

/* End the command: prepare JSON output, send it to the channel, free the context */
void command_end(command_context* ctx) {
    json_builder_end_object(ctx->jb);
    JsonGenerator *gen = json_generator_new();
    g_object_set(gen, "pretty", config_get_bool_opt("pretty_json", FALSE), NULL);
    json_generator_set_root(gen, json_builder_get_root(ctx->jb));

    gchar *str = json_generator_to_data(gen, NULL);
    g_object_unref(gen);
    g_object_unref(ctx->jb);

    interface_finalize(ctx->chan, str, FALSE);
    interface_finalize(ctx->chan, "\n", FALSE);
    g_free(str);
    g_free(ctx);    
}

/****************
 *** Commands ***
 ****************/
gboolean list_playlists(command_context* ctx) {
    int i, n, t;
    sp_playlist* pl;
    sp_playlist_type pt;
    const char* pn;
    gchar* pfn;

    if (!container_loaded()) {
        jb_add_string(ctx->jb, "error", "playlists container not loaded yet");
        return TRUE;
    }

    n = playlists_len();
    json_builder_set_member_name(ctx->jb, "playlists");
    json_builder_begin_array(ctx->jb);

    for (i=0; i<n; i++) {
        pt = playlist_type(i);
        switch(pt) {
        case SP_PLAYLIST_TYPE_START_FOLDER:
            g_debug("Playlist %d is a folder start", i);

            json_builder_begin_object(ctx->jb);
            pfn = playlist_folder_name(i);
            jb_add_string(ctx->jb, "name", pfn);
            g_free(pfn);

            jb_add_string(ctx->jb, "type", "folder");

            json_builder_set_member_name(ctx->jb, "playlists");
            json_builder_begin_array(ctx->jb);
            break;

        case SP_PLAYLIST_TYPE_END_FOLDER:
            g_debug("Playlist %d is a folder end", i);
            json_builder_end_array(ctx->jb);
            json_builder_end_object(ctx->jb);
            break;

        case SP_PLAYLIST_TYPE_PLAYLIST:
            pl = playlist_get(i);
            json_builder_begin_object(ctx->jb);
            if (!pl) {
                g_debug("Got NULL pointer when loading playlist %d.", i);
                json_builder_end_object(ctx->jb);
                break;
            }
            if (!sp_playlist_is_loaded(pl)) {
                g_debug("Playlist %d is not loaded.", i);
                json_builder_end_object(ctx->jb);
                break;
            }
            pn = sp_playlist_name(pl);

            if (g_strcmp0("-", pn)) {
                /* Regular playlist */
                t = sp_playlist_num_tracks(pl);

                jb_add_string(ctx->jb, "type", "playlist");
                jb_add_string(ctx->jb, "name", pn);
                jb_add_int(ctx->jb, "tracks", t);
                jb_add_int(ctx->jb, "index", i);
            }
            else {
                /* Playlist separator */
                jb_add_string(ctx->jb, "type", "separator");
            }
            json_builder_end_object(ctx->jb);
            break;

        default:
            g_debug("Playlist %d is a placeholder", i);
        }
    }

    json_builder_end_array(ctx->jb);
    return TRUE;
}

gboolean list_tracks(command_context* ctx, guint idx) {
    sp_playlist* pl;
    GArray* tracks;

    /* Get the playlist */
    pl = playlist_get(idx);
    if (!pl) {
        jb_add_string(ctx->jb, "error", "invalid playlist");
        return TRUE;
    }
    
    /* Get the tracks array */
    // FIXME
    tracks = tracks_get_playlist(pl);
    if (!tracks) {
        jb_add_string(ctx->jb, "error", "playlist not loaded yet");
        return TRUE;
    }

    json_builder_set_member_name(ctx->jb, "tracks");
    json_builder_begin_array(ctx->jb);
    json_tracks_array(tracks, ctx->jb);
    json_builder_end_array(ctx->jb);

    g_array_free(tracks, TRUE);
    return TRUE;
}


gboolean status(command_context* ctx) {
    sp_track* track;
    int track_nb, total_tracks, track_duration, track_position;
    queue_status qs;
    gchar* track_name;
    gchar* track_artist;
    gchar* track_album;
    gchar* track_link;
    
    qs = queue_get_status(&track, &track_nb, &total_tracks);

    jb_add_string(ctx->jb, "status",
                  (qs == PLAYING) ? "playing"
                  : ((qs == PAUSED) ? "paused" : "stopped"));

    jb_add_bool(ctx->jb, "repeat", queue_get_repeat());
    jb_add_bool(ctx->jb, "shuffle", queue_get_shuffle());
    jb_add_int(ctx->jb, "total_tracks", total_tracks);

    if (qs != STOPPED) {
        jb_add_int(ctx->jb, "current_track", track_nb+1);

        track_get_data(track, &track_name, &track_artist, &track_album, &track_link, &track_duration);
        track_position = session_play_time();

        jb_add_string(ctx->jb, "artist", track_artist);
        jb_add_string(ctx->jb, "title", track_name);
        jb_add_string(ctx->jb, "album", track_album);
        jb_add_int(ctx->jb, "duration", track_duration);
        jb_add_int(ctx->jb, "position", track_position);
        jb_add_string(ctx->jb, "uri", track_link);
        g_free(track_name);
        g_free(track_artist);
        g_free(track_album);
        g_free(track_link);
    }
    return TRUE;
}

gboolean repeat(command_context* ctx) {
    gboolean r = queue_get_repeat();
    queue_set_repeat(TRUE, !r);
    return status(ctx);
}

gboolean shuffle(command_context* ctx) {
    gboolean s = queue_get_shuffle();
    queue_set_shuffle(TRUE, !s);
    return status(ctx);
}


gboolean list_queue(command_context* ctx) {
    GArray* tracks;

    tracks = queue_tracks();
    if (!tracks)
        g_error("Couldn't read queue.");

    json_builder_set_member_name(ctx->jb, "tracks");
    json_builder_begin_array(ctx->jb);
    json_tracks_array(tracks, ctx->jb);
    json_builder_end_array(ctx->jb);
    g_array_free(tracks, TRUE);
    return TRUE;
}

gboolean clear_queue(command_context* ctx) {
    queue_clear(TRUE);
    return status(ctx);
}

gboolean remove_queue_items(command_context* ctx, guint first, guint last) {
    queue_remove_tracks(TRUE, first, last-first+1);
    return status(ctx);
}

gboolean remove_queue_item(command_context* ctx, guint idx) {
    return remove_queue_items(ctx, idx, idx);
}

gboolean play_playlist(command_context* ctx, guint idx) {
    sp_playlist* pl;

    /* First check the playlist type */
    if (playlist_type(idx) != SP_PLAYLIST_TYPE_PLAYLIST) {
        jb_add_string(ctx->jb, "error", "not a playlist");
        return TRUE;
    }

    /* Then get the playlist */
    pl = playlist_get(idx);

    if (!pl) {
        jb_add_string(ctx->jb, "error", "invalid playlist");
        return TRUE;
    }

    /* Load it and play it */
    queue_set_playlist(FALSE, pl);
    queue_play(TRUE);

    return status(ctx);
}

gboolean play_track(command_context* ctx, guint pl_idx, guint tr_idx) {
    sp_playlist* pl;
    sp_track* tr;
    GArray* tracks;

    /* First check the playlist type */
    if (playlist_type(pl_idx) != SP_PLAYLIST_TYPE_PLAYLIST) {
        jb_add_string(ctx->jb, "error", "not a playlist");
        return TRUE;
    }

    /* Then get the playlist */
    pl = playlist_get(pl_idx);
    if (!pl) {
        jb_add_string(ctx->jb, "error", "invalid playlist");
        return TRUE;
    }

    /* Then get the track itself */
    // FIXME
    tracks = tracks_get_playlist(pl);
    if (!tracks) {
        jb_add_string(ctx->jb, "error", "playlist not loaded yet");
        return TRUE;
    }
    if ((tr_idx <= 0) || (tr_idx > tracks->len)) {
        jb_add_string(ctx->jb, "error", "invalid track number");
        g_array_free(tracks, TRUE);
        return TRUE;
    }

    tr = g_array_index(tracks, sp_track*, tr_idx-1);
    g_array_free(tracks, TRUE);

    /* Load it and play it */
    queue_set_track(FALSE, tr);
    queue_play(TRUE);

    return status(ctx);
}

gboolean add_playlist(command_context* ctx, guint idx) {
    sp_playlist* pl;
    int tot;

    /* First check the playlist type */
    if (playlist_type(idx) != SP_PLAYLIST_TYPE_PLAYLIST) {
        jb_add_string(ctx->jb, "error", "not a playlist");
        return TRUE;
    }

    /* Then get the playlist */
    pl = playlist_get(idx);

    if (!pl) {
        jb_add_string(ctx->jb, "error", "invalid playlist");
        return TRUE;
    }

    /* Load it */
    queue_add_playlist(TRUE, pl);

    queue_get_status(NULL, NULL, &tot);
    jb_add_int(ctx->jb, "total_tracks", tot);
    return TRUE;
}

gboolean add_track(command_context* ctx, guint pl_idx, guint tr_idx) {
    sp_playlist* pl;
    sp_track* tr;
    GArray* tracks;
    int tot;

    /* First check the playlist type */
    if (playlist_type(pl_idx) != SP_PLAYLIST_TYPE_PLAYLIST) {
        jb_add_string(ctx->jb, "error", "not a playlist");
        return TRUE;
    }

    /* Then get the playlist */
    pl = playlist_get(pl_idx);
    if (!pl) {
        jb_add_string(ctx->jb, "error", "invalid playlist");
        return TRUE;
    }

    /* Then get the track itself */
    // FIXME
    tracks = tracks_get_playlist(pl);
    if (!tracks) {
        jb_add_string(ctx->jb, "error", "playlist not loaded yet");
        return TRUE;
    }
    if ((tr_idx <= 0) || (tr_idx > tracks->len)) {
        jb_add_string(ctx->jb, "error", "invalid track number");
        g_array_free(tracks, TRUE);
        return TRUE;
    }

    tr = g_array_index(tracks, sp_track*, tr_idx-1);

    /* Load it */
    queue_add_track(TRUE, tr);

    queue_get_status(NULL, NULL, &tot);
    jb_add_int(ctx->jb, "total_tracks", tot);
    return TRUE;
}

gboolean play(command_context* ctx) {
    queue_play(TRUE);
    return status(ctx);
}
gboolean stop(command_context* ctx) {
    queue_stop(TRUE);
    return status(ctx);
}
gboolean toggle(command_context* ctx) {
    queue_toggle(TRUE);
    return status(ctx);
}
gboolean seek(command_context* ctx, guint pos) {
    queue_seek(pos);
    return status(ctx);
}

gboolean goto_next(command_context* ctx) {
    queue_next(TRUE);
    return status(ctx);
}
gboolean goto_prev(command_context* ctx) {
    queue_prev(TRUE);
    return status(ctx);
}
gboolean goto_nb(command_context* ctx, guint nb) {
    queue_goto(TRUE, nb-1, TRUE);
    return status(ctx);
}

gboolean image(command_context* ctx) {
    sp_track* track = NULL;
    guchar* img_data;
    gsize len;
    gboolean res;

    queue_get_status(&track, NULL, NULL);
    res = track_get_image_data(track, (gpointer*) &img_data, &len);
    if (!res) {
        // FIXME
        jb_add_string(ctx->jb, "status", "not-loaded");
    }
    else if (!img_data) {
        jb_add_string(ctx->jb, "status", "absent");
    }
    else {
        gchar* b64data = g_base64_encode(img_data, len);
        jb_add_string(ctx->jb, "status", "ok");
        jb_add_string(ctx->jb, "data", b64data);
        
        g_free(b64data);
        g_free(img_data);
    }
    return TRUE;
}
