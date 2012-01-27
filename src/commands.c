/*
 * Copyright (C) 2010, 2011, 2012 Thomas Jost
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
#include "queue.h"
#include "spotify.h"
#include "utils.h"

/* {{{ JSON helpers */
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
    int track_duration, track_popularity;
    gchar* track_name;
    gchar* track_artist;
    gchar* track_album;
    gchar* track_link;

    /* For each track, add an object to the JSON array */
    for (i=0; i < tracks->len; i++) {
        track = g_array_index(tracks, sp_track*, i);
        if (!sp_track_is_loaded(track)) continue;

        track_avail = track_available(track);
        track_get_data(track, &track_name, &track_artist, &track_album, &track_link,
                       &track_duration, &track_popularity);

        json_builder_begin_object(jb);
        jb_add_string(jb, "artist", track_artist);
        jb_add_string(jb, "title", track_name);
        jb_add_string(jb, "album", track_album);
        jb_add_int(jb, "duration", track_duration);
        jb_add_string(jb, "uri", track_link);
        jb_add_bool(jb, "available", track_avail);
        jb_add_int(jb, "popularity", track_popularity);
        jb_add_int(jb, "index", i+1);
        json_builder_end_object(jb);

        g_free(track_name);
        g_free(track_artist);
        g_free(track_album);
        g_free(track_link);
    }
}
static void json_playlist_offline_status(sp_playlist* pl, JsonBuilder* jb) {
    sp_playlist_offline_status pos = playlist_get_offline_status(pl);
    json_builder_set_member_name(jb, "offline");
    switch(pos) {
    case SP_PLAYLIST_OFFLINE_STATUS_NO:
        json_builder_add_boolean_value(jb, FALSE); break;
    case SP_PLAYLIST_OFFLINE_STATUS_YES:
        json_builder_add_boolean_value(jb, TRUE); break;
    case SP_PLAYLIST_OFFLINE_STATUS_DOWNLOADING:
        json_builder_add_string_value(jb, "downloading");
        jb_add_int(jb, "offline_progress", playlist_get_offline_download_completed(pl));
        break;
    case SP_PLAYLIST_OFFLINE_STATUS_WAITING:
        json_builder_add_string_value(jb, "waiting"); break;
    default:
        json_builder_add_string_value(jb, "unknown");
    }
}
/* }}} */
/* {{{ Commands management */
#define CMD_CALLBACK_WAIT_TIME 100
#define CMD_CALLBACK_MAX_CALLS  30

/* Run the given command with the given arguments */
gboolean command_run(command_finalize_func finalize, gpointer finalize_data, command_descriptor* desc, int argc, char** argv) {
    gboolean ret = TRUE;
    command_context* ctx = g_new(command_context, 1);
    ctx->jb = json_builder_new();
    ctx->finalize = finalize;
    ctx->finalize_data = finalize_data;
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
    else if (desc->args[0] == CA_STR) {
        const gchar* arg1 = argv[1];
        if (desc->args[1] == CA_NONE) {
            gboolean (*cmd)(command_context*, const gchar*) = desc->func;
            ret = cmd(ctx, arg1);
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

/* End the command: prepare JSON output, finalize it (most of the time send it to an IO channel), free the context */
void command_end(command_context* ctx) {
    json_builder_end_object(ctx->jb);
    JsonGenerator* gen = json_generator_new();
    g_object_set(gen, "pretty", config_get_bool_opt("pretty_json", FALSE), NULL);
    json_generator_set_root(gen, json_builder_get_root(ctx->jb));

    gchar* str = json_generator_to_data(gen, NULL);
    g_object_unref(gen);
    g_object_unref(ctx->jb);

    gchar* strn = g_strconcat(str, "\n", NULL);
    g_free(str);

    ctx->finalize(strn, ctx->finalize_data);
    g_free(strn);
    g_free(ctx);
}
/* }}} */

/****************
 *** Commands ***
 ****************/
/* {{{ Lists */
gboolean list_playlists(command_context* ctx) {
    int i, n, t;
    sp_playlist* pl;
    sp_playlist_type pt;
    const char* pn;
    gchar* pfn;

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
                json_playlist_offline_status(pl, ctx->jb);
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

    jb_add_string(ctx->jb, "name", sp_playlist_name(pl));
    const gchar* desc = sp_playlist_get_description(pl);
    if (desc) {
        jb_add_string(ctx->jb, "description", desc);
    }

    json_builder_set_member_name(ctx->jb, "tracks");
    json_builder_begin_array(ctx->jb);
    json_tracks_array(tracks, ctx->jb);
    json_builder_end_array(ctx->jb);
    g_array_free(tracks, TRUE);

    json_playlist_offline_status(pl, ctx->jb);

    return TRUE;
}
/* }}} */
/* {{{ Status and play mode */
gboolean status(command_context* ctx) {
    sp_track* track;
    int track_nb, total_tracks, track_duration, track_position, track_popularity;
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

        track_get_data(track, &track_name, &track_artist, &track_album, &track_link,
                       &track_duration, &track_popularity);
        track_position = session_play_time();

        jb_add_string(ctx->jb, "artist", track_artist);
        jb_add_string(ctx->jb, "title", track_name);
        jb_add_string(ctx->jb, "album", track_album);
        jb_add_int(ctx->jb, "duration", track_duration);
        jb_add_int(ctx->jb, "position", track_position);
        jb_add_string(ctx->jb, "uri", track_link);
        jb_add_int(ctx->jb, "popularity", track_popularity);
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
/* }}} */
/* {{{ Queue */
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
    queue_remove_tracks(TRUE, first-1, last-first+1);
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
/* }}} */
/* {{{ Playback and navigation */
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
/* }}} */
/* {{{ Offline sync */
gboolean offline_status(command_context* ctx) {
    sp_offline_sync_status status;
    gboolean sync_in_progress;
    int tracks_to_sync, num_playlists, time_left;

    session_get_offline_sync_status(&status, &sync_in_progress, &tracks_to_sync,
                                    &num_playlists, &time_left);

    jb_add_int(ctx->jb, "offline_playlists", num_playlists);
    jb_add_int(ctx->jb, "tracks_to_sync", tracks_to_sync);
    jb_add_bool(ctx->jb, "sync_in_progress", sync_in_progress);
    if (sync_in_progress) {
        jb_add_int(ctx->jb, "tracks_done", status.done_tracks);
        jb_add_int(ctx->jb, "tracks_copied", status.copied_tracks);
        jb_add_int(ctx->jb, "tracks_queued", status.queued_tracks);
        jb_add_int(ctx->jb, "tracks_error", status.error_tracks);
        jb_add_int(ctx->jb, "tracks_willnotcopy", status.willnotcopy_tracks);
    }
    jb_add_int(ctx->jb, "time_before_relogin", time_left);

    return TRUE;    
}

gboolean offline_toggle(command_context* ctx, guint idx) {
    sp_playlist* pl = playlist_get(idx);
    if (!pl) {
        jb_add_string(ctx->jb, "error", "invalid playlist");
        return TRUE;
    }

    sp_playlist_offline_status pos = playlist_get_offline_status(pl);
    gboolean mode = (pos != SP_PLAYLIST_OFFLINE_STATUS_NO);
    playlist_set_offline_mode(pl, !mode);

    jb_add_bool(ctx->jb, "offline", !mode);
    return TRUE;
}
/* }}} */
/* {{{ Image */
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
/* }}} */
/* {{{ URIs */
  /* {{{ uri_info callbacks */
/* Callback (from uri_info) to get album data */
static void _uri_info_album_cb(sp_albumbrowse* ab, gpointer userdata) {
    command_context* ctx = (command_context*) userdata;

    /* Check for error */
    sp_error err = sp_albumbrowse_error(ab);
    if (err != SP_ERROR_OK) {
        jb_add_string(ctx->jb, "error", sp_error_message(err));
        goto _uiac_clean;
    }

    sp_album* album = sp_albumbrowse_album(ab);
    sp_artist* artist = sp_albumbrowse_artist(ab);

    jb_add_string(ctx->jb, "title", sp_album_name(album));
    jb_add_string(ctx->jb, "artist", sp_artist_name(artist));
    jb_add_int(ctx->jb, "year", sp_album_year(album));
        
    sp_albumtype type = sp_album_type(album);
    json_builder_set_member_name(ctx->jb, "album_type");
    if (type == SP_ALBUMTYPE_ALBUM)
        json_builder_add_string_value(ctx->jb, "album");
    else if (type == SP_ALBUMTYPE_SINGLE)
        json_builder_add_string_value(ctx->jb, "single");
    else if (type == SP_ALBUMTYPE_COMPILATION)
        json_builder_add_string_value(ctx->jb, "compilation");
    else
        json_builder_add_string_value(ctx->jb, "unknown");

    GArray* tracks;
    int n = sp_albumbrowse_num_tracks(ab);
    tracks = g_array_sized_new(FALSE, FALSE, sizeof(sp_track*), n);
    if (!tracks)
        g_error("Can't allocate array of %d tracks.", n);

    size_t i;
    for (i=0; i < n; i++) {
        sp_track* tr = sp_albumbrowse_track(ab, i);
        g_array_append_val(tracks, tr);
    }
    json_builder_set_member_name(ctx->jb, "tracks");
    json_builder_begin_array(ctx->jb);
    json_tracks_array(tracks, ctx->jb);
    json_builder_end_array(ctx->jb);
    g_array_free(tracks, TRUE);

    jb_add_string(ctx->jb, "review", sp_albumbrowse_review(ab));

 _uiac_clean:
    sp_albumbrowse_release(ab);
    command_end(ctx);
}

/* Callback (from uri_info) to get artist data */
static void _uri_info_artist_cb(sp_artistbrowse* arb, gpointer userdata) {
    command_context* ctx = (command_context*) userdata;
    gchar uri[1024];
    int i, n;

    /* Check for error */
    sp_error err = sp_artistbrowse_error(arb);
    if (err != SP_ERROR_OK) {
        jb_add_string(ctx->jb, "error", sp_error_message(err));
        goto _uiarc_clean;
    }

    sp_artist* artist = sp_artistbrowse_artist(arb);
    jb_add_string(ctx->jb, "artist", sp_artist_name(artist));

    /* Tracks... */
    n = sp_artistbrowse_num_tracks(arb);
    GArray* tracks = g_array_sized_new(FALSE, FALSE, sizeof(sp_track*), n);
    if (!tracks)
        g_error("Can't allocate array of %d tracks.", n);
    for (i=0; i < n; i++) {
        sp_track* tr = sp_artistbrowse_track(arb, i);
        g_array_append_val(tracks, tr);
    }

    json_builder_set_member_name(ctx->jb, "tracks");
    json_builder_begin_array(ctx->jb);
    json_tracks_array(tracks, ctx->jb);
    json_builder_end_array(ctx->jb);
    g_array_free(tracks, TRUE);

    /* Albums... */
    n = sp_artistbrowse_num_albums(arb);
    json_builder_set_member_name(ctx->jb, "albums");
    json_builder_begin_array(ctx->jb);
    for (i=0; i < n; i++) {
        sp_album* alb = sp_artistbrowse_album(arb, i);
        json_builder_begin_object(ctx->jb);

        sp_artist* albart = sp_album_artist(alb);
        jb_add_string(ctx->jb, "artist", sp_artist_name(albart));

        jb_add_string(ctx->jb, "title", sp_album_name(alb));
        jb_add_bool(ctx->jb, "available", sp_album_is_available(alb));

        sp_link* lnk = sp_link_create_from_album(alb);
        if (sp_link_as_string(lnk, uri, 1024) < 1024) {
            jb_add_string(ctx->jb, "uri", uri);
        }
        sp_link_release(lnk);

        json_builder_end_object(ctx->jb);
    }
    json_builder_end_array(ctx->jb);

    /* Similar artists... */
    n = sp_artistbrowse_num_similar_artists(arb);
    json_builder_set_member_name(ctx->jb, "similar_artists");
    json_builder_begin_array(ctx->jb);
    for (i=0; i < n; i++) {
        sp_artist* simart = sp_artistbrowse_similar_artist(arb, i);
        json_builder_begin_object(ctx->jb);

        jb_add_string(ctx->jb, "artist", sp_artist_name(simart));

        sp_link* lnk = sp_link_create_from_artist(simart);
        if (sp_link_as_string(lnk, uri, 1024) < 1024) {
            jb_add_string(ctx->jb, "uri", uri);
        }
        sp_link_release(lnk);

        json_builder_end_object(ctx->jb);
    }
    json_builder_end_array(ctx->jb);

    jb_add_string(ctx->jb, "biography", sp_artistbrowse_biography(arb));

 _uiarc_clean:
    sp_artistbrowse_release(arb);
    command_end(ctx);
}

/* Callback (from uri_info or timeout) to get playlist data */
static gboolean _uri_info_playlist_cb(gpointer* data) {
    command_context* ctx = data[0];
    size_t count = (size_t) ++data[1];
    sp_playlist* pl = data[2];
    sp_link* lnk = data[3];

    /* If not loaded, wait a little more */
    if (!sp_playlist_is_loaded(pl)) {
        if (count < CMD_CALLBACK_MAX_CALLS)
            return TRUE;
        else {
            jb_add_string(ctx->jb, "error", "playlist not loaded");
            goto _uipc_clean;
        }
    }

    /* Make sure all tracks are loaded */
    GArray* tracks = tracks_get_playlist(pl);
    int i;
    for (i=0; i < tracks->len; i++) {
        sp_track* track = g_array_index(tracks, sp_track*, i);
        if (!sp_track_is_loaded(track)) {
            g_array_free(tracks, TRUE);
            return TRUE;
        }
    }

    jb_add_string(ctx->jb, "name", sp_playlist_name(pl));
    const gchar* desc = sp_playlist_get_description(pl);
    if (desc) {
        jb_add_string(ctx->jb, "description", desc);
    }

    json_builder_set_member_name(ctx->jb, "tracks");
    json_builder_begin_array(ctx->jb);
    json_tracks_array(tracks, ctx->jb);
    json_builder_end_array(ctx->jb);

    g_array_free(tracks, TRUE);

 _uipc_clean:
    sp_link_release(lnk);
    g_free(data);
    command_end(ctx);
    return FALSE;
}

/* Callback (from uri_info or timeout) to get track data */
static gboolean _uri_info_track_cb(gpointer* data) {
    command_context* ctx = data[0];
    size_t count = (size_t) ++data[1];
    sp_track* track = data[2];
    int offset = *(int*) data[3];
    sp_link* lnk = data[4];

    /* If not loaded, wait a little more */
    if (!sp_track_is_loaded(track)) {
        if (count < CMD_CALLBACK_MAX_CALLS)
            return TRUE;
        else {
            jb_add_string(ctx->jb, "error", "track not loaded");
            goto _uitcb_clean;
        }
    }

    gchar* name;
    gchar* artist;
    gchar* album;
    int duration, popularity;
    track_get_data(track, &name, &artist, &album, NULL, &duration, &popularity);
    gboolean available = track_available(track);

    jb_add_string(ctx->jb, "artist", artist);
    jb_add_string(ctx->jb, "title", name);
    jb_add_string(ctx->jb, "album", album);
    jb_add_int(ctx->jb, "duration", duration);
    jb_add_int(ctx->jb, "offset", offset);
    jb_add_bool(ctx->jb, "available", available);
    jb_add_int(ctx->jb, "popularity", popularity);

    g_free(name);
    g_free(artist);
    g_free(album);

 _uitcb_clean:
    sp_link_release(lnk);
    g_free(data[3]);
    g_free(data);

    command_end(ctx);
    return FALSE;
}
  /* }}} */
  /* {{{ uri_add/uri_play callbacks */
static void _uri_add_album_cb(sp_albumbrowse* ab, gpointer userdata) {
    gpointer* data = (gpointer*) userdata;
    command_context* ctx = data[0];
    gboolean play = *(gboolean*) data[1];
    g_free(data);

    int n = sp_albumbrowse_num_tracks(ab);

    if (play)
        queue_clear(FALSE);

    size_t i;
    for (i=0; i < n; i++) {
        sp_track* tr = sp_albumbrowse_track(ab, i);
        queue_add_track(FALSE, tr);
    }
    
    if (play) {
        queue_play(TRUE);
        status(ctx);
    }
    else {
        int tot;
        queue_notify();
        queue_get_status(NULL, NULL, &tot);
        jb_add_int(ctx->jb, "total_tracks", tot);
    }

    sp_albumbrowse_release(ab);
    command_end(ctx);
}

static gboolean _uri_add_playlist_cb(gpointer* data) {
    command_context* ctx = data[0];
    size_t count = (size_t) ++data[1];
    sp_playlist* pl = data[2];
    sp_link* lnk = data[3];
    gboolean play = *(gboolean*) data[4];

    /* If not loaded, wait a little more */
    if (!sp_playlist_is_loaded(pl)) {
        if (count < CMD_CALLBACK_MAX_CALLS)
            return TRUE;
        else {
            jb_add_string(ctx->jb, "error", "playlist not loaded");
            goto _uapcb_clean;
        }
    }

    /* Make sure all tracks are loaded */
    GArray* tracks = tracks_get_playlist(pl);
    int i;
    for (i=0; i < tracks->len; i++) {
        sp_track* track = g_array_index(tracks, sp_track*, i);
        if (!sp_track_is_loaded(track)) {
            g_array_free(tracks, TRUE);
            return TRUE;
        }
    }

    if (play) {
        queue_set_playlist(FALSE, pl);
        queue_play(TRUE);
        status(ctx);
    }
    else {
        queue_add_playlist(TRUE, pl);

        int tot;
        queue_get_status(NULL, NULL, &tot);
        jb_add_int(ctx->jb, "total_tracks", tot);
    }

    g_array_free(tracks, TRUE);

 _uapcb_clean:
    sp_link_release(lnk);
    g_free(data[4]);
    g_free(data);

    command_end(ctx);
    return FALSE;
}

static gboolean _uri_add_track_cb(gpointer* data) {
    command_context* ctx = data[0];
    size_t count = (size_t) ++data[1];
    sp_track* track = data[2];
    int offset = *(int*) data[3];
    sp_link* lnk = data[4];
    gboolean play = *(gboolean*) data[5];

    /* If not loaded, wait a little more */
    if (!sp_track_is_loaded(track)) {
        if (count < CMD_CALLBACK_MAX_CALLS)
            return TRUE;
        else {
            jb_add_string(ctx->jb, "error", "track not loaded");
            goto _uatcb_clean;
        }
    }

    if (play) {
        queue_set_track(FALSE, track);
        queue_play(TRUE);
        if (offset > 0)
            queue_seek(offset);
        status(ctx);
    }
    else {
        queue_add_track(TRUE, track);

        int tot;
        queue_get_status(NULL, NULL, &tot);
        jb_add_int(ctx->jb, "total_tracks", tot);
    }

 _uatcb_clean:
    sp_link_release(lnk);
    g_free(data[3]);
    g_free(data[5]);
    g_free(data);

    command_end(ctx);
    return FALSE;
}
  /* }}} */

gboolean uri_info(command_context* ctx, sp_link* lnk) {
    sp_linktype type = sp_link_type(lnk);
    gboolean done = TRUE;

    switch(type) {
    case SP_LINKTYPE_INVALID:
        jb_add_string(ctx->jb, "type", "invalid");
        sp_link_release(lnk);
        break;

    case SP_LINKTYPE_TRACK: {
        jb_add_string(ctx->jb, "type", "track");

        int offset;
        sp_track* track = sp_link_as_track_and_offset(lnk, &offset);
        if (!track) {
            jb_add_string(ctx->jb, "error", "can't retrieve track");
            sp_link_release(lnk);
            break;
        }
        gpointer* data = g_new(gpointer, 5);
        data[0] = ctx;
        data[1] = 0;
        data[2] = track;
        data[3] = g_new(int, 1);
        *(int*) data[3] = offset;
        data[4] = lnk;
        if (!sp_track_is_loaded(track))
            g_timeout_add(CMD_CALLBACK_WAIT_TIME, (GSourceFunc) _uri_info_track_cb, data);
        else
            _uri_info_track_cb(data);
        done = FALSE;

        break;
    }
    case SP_LINKTYPE_ALBUM: {
        jb_add_string(ctx->jb, "type", "album");

        sp_album* album = sp_link_as_album(lnk);
        if (!album) {
            jb_add_string(ctx->jb, "error", "can't retrieve album");
            sp_link_release(lnk);
            break;
        }
        done = FALSE;
        albumbrowse_create(album, _uri_info_album_cb, ctx);
        sp_link_release(lnk);
        break;
    }
    case SP_LINKTYPE_ARTIST: {
        jb_add_string(ctx->jb, "type", "artist");

        sp_artist* artist = sp_link_as_artist(lnk);
        if (!artist) {
            jb_add_string(ctx->jb, "error", "can't retrieve artist");
            sp_link_release(lnk);
            break;
        }
        done = FALSE;
        artistbrowse_create(artist, _uri_info_artist_cb, ctx);
        sp_link_release(lnk);
        break;
    }
    case SP_LINKTYPE_PLAYLIST: {
        jb_add_string(ctx->jb, "type", "playlist");

        sp_playlist* pl = playlist_get_from_link(lnk);
        if (!pl) {
            jb_add_string(ctx->jb, "error", "can't retrieve playlist");
            sp_link_release(lnk);
            break;
        }
        gpointer* data = g_new(gpointer, 4);
        data[0] = ctx;
        data[1] = 0;
        data[2] = pl;
        data[3] = lnk;
        if (!sp_playlist_is_loaded(pl))
            g_timeout_add(CMD_CALLBACK_WAIT_TIME, (GSourceFunc) _uri_info_playlist_cb, data);
        else
            _uri_info_playlist_cb(data);
        done = FALSE;

        break;
    }
    default:
        jb_add_string(ctx->jb, "type", "not implemented");
        sp_link_release(lnk);
        break;
    }

    return done;
}

static gboolean _uri_add_play(command_context* ctx, sp_link* lnk, gboolean play) {
    sp_linktype type = sp_link_type(lnk);
    gboolean done = TRUE;

    switch(type) {
    case SP_LINKTYPE_INVALID:
        jb_add_string(ctx->jb, "error", "invalid URI");
        sp_link_release(lnk);
        break;
    case SP_LINKTYPE_TRACK: {
        int offset;
        sp_track* track = sp_link_as_track_and_offset(lnk, &offset);
        if (!track) {
            jb_add_string(ctx->jb, "error", "can't retrieve track");
            sp_link_release(lnk);
            break;
        }
        gpointer* data = g_new(gpointer, 6);
        data[0] = ctx;
        data[1] = 0;
        data[2] = track;
        data[3] = g_memdup(&offset, sizeof(int));
        data[4] = lnk;
        data[5] = g_memdup(&play, sizeof(gboolean));
        if (!sp_track_is_loaded(track))
            g_timeout_add(CMD_CALLBACK_WAIT_TIME, (GSourceFunc) _uri_add_track_cb, data);
        else
            _uri_add_track_cb(data);
        done = FALSE;

        break;
    }
    case SP_LINKTYPE_ALBUM: {
        sp_album* album = sp_link_as_album(lnk);
        if (!album) {
            jb_add_string(ctx->jb, "error", "can't retrieve album");
            sp_link_release(lnk);
            break;
        }
        done = FALSE;
        gpointer* data = g_new(gpointer, 2);
        data[0] = ctx;
        data[1] = g_memdup(&play, sizeof(gboolean));
        albumbrowse_create(album, _uri_add_album_cb, data);
        break;
    }
    case SP_LINKTYPE_PLAYLIST: {
        sp_playlist* pl = playlist_get_from_link(lnk);
        if (!pl) {
            jb_add_string(ctx->jb, "error", "can't retrieve playlist");
            sp_link_release(lnk);
            break;
        }
        gpointer* data = g_new(gpointer, 5);
        data[0] = ctx;
        data[1] = 0;
        data[2] = pl;
        data[3] = lnk;
        data[4] = g_memdup(&play, sizeof(gboolean));
        if (!sp_playlist_is_loaded(pl))
            g_timeout_add(CMD_CALLBACK_WAIT_TIME, (GSourceFunc) _uri_add_playlist_cb, data);
        else
            _uri_add_playlist_cb(data);
        done = FALSE;

        break;
    }
    default:
        jb_add_string(ctx->jb, "error", "not implemented");
        sp_link_release(lnk);
        break;
    }

    return done;
}

gboolean uri_add(command_context* ctx, sp_link* lnk) {
    return _uri_add_play(ctx, lnk, FALSE);
}

gboolean uri_play(command_context* ctx, sp_link* lnk) {
    return _uri_add_play(ctx, lnk, TRUE);
}
/* }}} */
/* {{{ Search */
static void _search_cb(sp_search* srch, gpointer userdata) {
    command_context* ctx = (command_context*) userdata;
    int i, n;

    /* Check for error */
    sp_error err = sp_search_error(srch);
    if (err != SP_ERROR_OK) {
        jb_add_string(ctx->jb, "error", sp_error_message(err));
        goto _s_cb_clean;
    }

    /* Basic things first */
    jb_add_string(ctx->jb, "query", sp_search_query(srch));
    const gchar* dym = sp_search_did_you_mean(srch);
    if (dym[0] != '\0') {
        jb_add_string(ctx->jb, "did_you_mean", dym);
    }

    sp_link* lnk = sp_link_create_from_search(srch);
    gchar uri[1024];
    if (sp_link_as_string(lnk, uri, 1024) < 1024) {
        /* FIXME: what to do if >= 1024? */
        jb_add_string(ctx->jb, "uri", uri);
    }
    sp_link_release(lnk);

    /* Now tracks... */
    jb_add_int(ctx->jb, "total_tracks", sp_search_total_tracks(srch));

    n = sp_search_num_tracks(srch);
    GArray* tracks = g_array_sized_new(FALSE, FALSE, sizeof(sp_track*), n);
    if (!tracks)
        g_error("Can't allocate array of %d tracks.", n);
    for (i=0; i < n; i++) {
        sp_track* tr = sp_search_track(srch, i);
        g_array_append_val(tracks, tr);
    }

    json_builder_set_member_name(ctx->jb, "tracks");
    json_builder_begin_array(ctx->jb);
    json_tracks_array(tracks, ctx->jb);
    json_builder_end_array(ctx->jb);
    g_array_free(tracks, TRUE);

    /* Albums... */
    jb_add_int(ctx->jb, "total_albums", sp_search_total_albums(srch));

    n = sp_search_num_albums(srch);
    json_builder_set_member_name(ctx->jb, "albums");
    json_builder_begin_array(ctx->jb);
    for (i=0; i < n; i++) {
        sp_album* alb = sp_search_album(srch, i);
        json_builder_begin_object(ctx->jb);

        sp_artist* artist = sp_album_artist(alb);
        jb_add_string(ctx->jb, "artist", sp_artist_name(artist));

        jb_add_string(ctx->jb, "title", sp_album_name(alb));
        jb_add_bool(ctx->jb, "available", sp_album_is_available(alb));

        lnk = sp_link_create_from_album(alb);
        if (sp_link_as_string(lnk, uri, 1024) < 1024) {
            jb_add_string(ctx->jb, "uri", uri);
        }
        sp_link_release(lnk);

        json_builder_end_object(ctx->jb);
    }
    json_builder_end_array(ctx->jb);

    /* Artists... */
    jb_add_int(ctx->jb, "total_artists", sp_search_total_artists(srch));

    n = sp_search_num_artists(srch);
    json_builder_set_member_name(ctx->jb, "artists");
    json_builder_begin_array(ctx->jb);
    for (i=0; i < n; i++) {
        sp_artist* artist = sp_search_artist(srch, i);
        json_builder_begin_object(ctx->jb);

        jb_add_string(ctx->jb, "artist", sp_artist_name(artist));

        lnk = sp_link_create_from_artist(artist);
        if (sp_link_as_string(lnk, uri, 1024) < 1024) {
            jb_add_string(ctx->jb, "uri", uri);
        }
        sp_link_release(lnk);

        json_builder_end_object(ctx->jb);
    }
    json_builder_end_array(ctx->jb);

    /* And we're done! */
 _s_cb_clean:
    sp_search_release(srch);
    command_end(ctx);
}

gboolean search(command_context* ctx, const gchar* query) {
    sp_search* srch = search_create(query, _search_cb, ctx);
    if (srch)
        return FALSE;
    else {
        jb_add_string(ctx->jb, "error", "can't create search");
        return TRUE;
    }
}
/* }}} */
