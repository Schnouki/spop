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
#include <libspotify/api.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "spop.h"
#include "commands.h"
#include "queue.h"
#include "spotify.h"
#include "utils.h"

/****************
 *** Commands ***
 ****************/
void list_playlists(GString* result) {
    int i, n, t;
    sp_playlist* pl;
    sp_playlist_type pt;
    const char* pn;
    gchar* pfn;
    gboolean in_folder = FALSE;

    if (!container_loaded()) {
        g_string_assign(result, "- playlists container not loaded yet\n");
        return;
    }

    n = playlists_len();
    if (n == -1) {
        g_warning("Could not determine the number of playlists");
        return;
    }

    for (i=0; i<n; i++) {
        pt = playlist_type(i);
        switch(pt) {
        case SP_PLAYLIST_TYPE_START_FOLDER:
            g_debug("Playlist %d is a folder start", i);
            in_folder = TRUE;
            g_string_append_line_number(result, i, n);
            pfn = playlist_folder_name(i);
            g_string_append_printf(result, " + %s\n", pfn);
            g_free(pfn);
            break;

        case SP_PLAYLIST_TYPE_END_FOLDER:
            g_debug("Playlist %d is a folder end", i);
            in_folder = FALSE;
            g_string_append_line_number(result, i, n);
            g_string_append_printf(result, " `--\n");
            break;

        case SP_PLAYLIST_TYPE_PLAYLIST:
            pl = playlist_get(i);
            if (!pl) {
                g_debug("Got NULL pointer when loading playlist %d.", i);
                break;
            }
            if (!sp_playlist_is_loaded(pl)) {
                g_debug("Playlist %d is not loaded.", i);
                break;
            }
            t = sp_playlist_num_tracks(pl);
            pn = sp_playlist_name(pl);
            g_string_append_line_number(result, i, n);

            if (g_strcmp0("-", pn)) {
                /* Regular playlist */
                g_string_append_printf(result, " %s%s (%d)\n",
                                       in_folder ? "| " : "",
                                       pn, t);
            }
            else {
                /* Playlist separator */
                g_string_append_printf(result, " %s--------------------\n",
                                       in_folder ? "| " : "");
            }
            break;

        default:
            g_debug("Playlist %d is a placeholder", i);
        }
    }
}

void list_tracks(int idx, GString* result) {
    sp_playlist* pl;
    GArray* tracks;

    /* Get the playlist */
    pl = playlist_get(idx);
    if (!pl) {
        g_string_assign(result, "- invalid playlist\n");
        return;
    }
    
    /* Get the tracks array */
    tracks = tracks_get_playlist(pl);
    if (!tracks) {
        g_string_assign(result, "- playlist not loaded yet\n");
        return;
    }

    format_tracks_array(tracks, result);
    g_array_free(tracks, TRUE);
}


void status(GString* result) {
    sp_track* track;
    int track_nb, total_tracks;
    queue_status qs;
    int track_min, track_sec, pos_min, pos_sec;
    gchar* track_name;
    gchar* track_artist;
    gchar* track_album;
    gchar* track_link;

    qs = queue_get_status(&track, &track_nb, &total_tracks);

    g_string_assign(result, "Status: ");
    if (qs == PLAYING) g_string_append(result, "playing");
    else if (qs == PAUSED) g_string_append(result, "paused");
    else g_string_append(result, "stopped");

    g_string_append_printf(result, "\nTotal tracks: %d\n", total_tracks);

    g_string_append(result, "Mode: ");
    if (queue_get_repeat()) {
        g_string_append(result, "repeat");
        if (queue_get_shuffle())
            g_string_append(result, " shuffle");
    }
    else {
        if (queue_get_shuffle())
            g_string_append(result, "shuffle");
        else
            g_string_append(result, "normal");
    }
    g_string_append(result, "\n");

    if (qs != STOPPED) {
        g_string_append_printf(result, "Current track: %d\n", track_nb+1);

        track_get_data(track, &track_name, &track_artist, &track_album, &track_link, &track_min, &track_sec);
        pos_sec = session_play_time();
        pos_min = pos_sec / 60;
        pos_sec %= 60;

        g_string_append_printf(result, "Artist: %s\nTitle: %s\nAlbum: %s\n",
                               track_artist, track_name, track_album);
        g_string_append_printf(result, "Duration: %d:%02d\nPosition: %d:%02d\nURI: %s\n",
                               track_min, track_sec, pos_min, pos_sec, track_link);
        g_free(track_name);
        g_free(track_artist);
        g_free(track_album);
        g_free(track_link);
    }
}

void repeat(GString* result) {
    gboolean r = queue_get_repeat();
    queue_set_repeat(TRUE, !r);
    status(result);
}

void shuffle(GString* result) {
    gboolean s = queue_get_shuffle();
    queue_set_shuffle(TRUE, !s);
    status(result);
}


void list_queue(GString* result) {
    GArray* tracks;

    tracks = queue_tracks();
    if (!tracks)
        g_error("Couldn't read queue.");

    format_tracks_array(tracks, result);
    g_array_free(tracks, TRUE);
}

void clear_queue(GString* result) {
    queue_clear(TRUE);
    status(result);
}

void remove_queue_items(int first, int nb, GString* result) {
    queue_remove_tracks(TRUE, first, nb);
    status(result);
}


void play_playlist(int idx, GString* result) {
    sp_playlist* pl;

    /* First check the playlist type */
    if (playlist_type(idx) != SP_PLAYLIST_TYPE_PLAYLIST) {
        g_string_assign(result, "- not a playlist\n");
        return;
    }

    /* Then get the playlist */
    pl = playlist_get(idx);

    if (!pl) {
        g_string_assign(result, "- invalid playlist\n");
        return;
    }

    /* Load it and play it */
    queue_set_playlist(FALSE, pl);
    queue_play(TRUE);

    status(result);
}

void play_track(int pl_idx, int tr_idx, GString* result) {
    sp_playlist* pl;
    sp_track* tr;
    GArray* tracks;

    /* First check the playlist type */
    if (playlist_type(pl_idx) != SP_PLAYLIST_TYPE_PLAYLIST) {
        g_string_assign(result, "- not a playlist\n");
        return;
    }

    /* Then get the playlist */
    pl = playlist_get(pl_idx);
    if (!pl) {
        g_string_assign(result, "- invalid playlist\n");
        return;
    }

    /* Then get the track itself */
    tracks = tracks_get_playlist(pl);
    if (!tracks) {
        g_string_assign(result, "- playlist not loaded yet.\n");
        return;
    }
    if ((tr_idx <= 0) || (tr_idx > tracks->len)) {
        g_string_assign(result, "- invalid track number\n");
        g_array_free(tracks, TRUE);
        return;
    }

    tr = g_array_index(tracks, sp_track*, tr_idx-1);
    g_array_free(tracks, TRUE);

    /* Load it and play it */
    queue_set_track(FALSE, tr);
    queue_play(TRUE);

    status(result);
}

void add_playlist(int idx, GString* result) {
    sp_playlist* pl;
    int tot;

    /* First check the playlist type */
    if (playlist_type(idx) != SP_PLAYLIST_TYPE_PLAYLIST) {
        g_string_assign(result, "- not a playlist\n");
        return;
    }

    /* Then get the playlist */
    pl = playlist_get(idx);

    if (!pl) {
        g_string_assign(result, "- invalid playlist\n");
        return;
    }

    /* Load it */
    queue_add_playlist(TRUE, pl);

    queue_get_status(NULL, NULL, &tot);
    g_string_printf(result, "Total tracks: %d\n", tot);
}

void add_track(int pl_idx, int tr_idx, GString* result) {
    sp_playlist* pl;
    sp_track* tr;
    GArray* tracks;
    int tot;

    /* First check the playlist type */
    if (playlist_type(pl_idx) != SP_PLAYLIST_TYPE_PLAYLIST) {
        g_string_assign(result, "- not a playlist\n");
        return;
    }

    /* Then get the playlist */
    pl = playlist_get(pl_idx);
    if (!pl) {
        g_string_assign(result, "- invalid playlist\n");
        return;
    }

    /* Then get the track itself */
    tracks = tracks_get_playlist(pl);
    if (!tracks) {
        g_string_assign(result, "- playlist not loaded yet.\n");
        return;
    }
    if ((tr_idx <= 0) || (tr_idx > tracks->len)) {
        g_string_assign(result, "- invalid track number\n");
        g_array_free(tracks, TRUE);
        return;
    }

    tr = g_array_index(tracks, sp_track*, tr_idx-1);

    /* Load it */
    queue_add_track(TRUE, tr);

    queue_get_status(NULL, NULL, &tot);
    g_string_printf(result, "Total tracks: %d\n", tot);
}

void play(GString* result) {
    queue_play(TRUE);
    status(result);
}
void stop(GString* result) {
    queue_stop(TRUE);
    status(result);
}
void toggle(GString* result) {
    queue_toggle(TRUE);
    status(result);
}
void seek(int pos, GString* result) {
    queue_seek(pos);
    status(result);
}

void goto_next(GString* result) {
    queue_next(TRUE);
    status(result);
}
void goto_prev(GString* result) {
    queue_prev(TRUE);
    status(result);
}
void goto_nb(GString* result, int nb) {
    queue_goto(TRUE, nb-1, TRUE);
    status(result);
}


/************************
 *** Helper functions ***
 ************************/
void format_tracks_array(GArray* tracks, GString* dst) {
    int i;
    sp_track* track;

    bool track_avail;
    int track_min, track_sec;
    gchar* track_name;
    gchar* track_artist;
    gchar* track_album;
    gchar* track_link;

    /* If the playlist is empty, just add a newline (an empty string would mean "error") */
    if (tracks->len == 0) {
        g_string_assign(dst, "\n");
        return;
    }

    /* For each track, add a line to the dst string */
    for (i=0; i < tracks->len; i++) {
        track = g_array_index(tracks, sp_track*, i);
        if (!sp_track_is_loaded(track)) continue;

        track_avail = track_available(track);
        track_get_data(track, &track_name, &track_artist, &track_album, &track_link, &track_min, &track_sec);

        g_string_append_line_number(dst, i+1, tracks->len+1);
        g_string_append_printf(dst, "%s %s -- \"%s\" -- \"%s\" (%d:%02d) URI:%s\n",
                               (track_avail ? "" : "-"), track_artist,
                               track_album, track_name, track_min, track_sec, 
                               track_link);
        g_free(track_name);
        g_free(track_artist);
        g_free(track_album);
        g_free(track_link);
    }
}

