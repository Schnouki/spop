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

#include <glib.h>
#include <libspotify/api.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "spop.h"
#include "commands.h"
#include "spotify.h"

/****************
 *** Commands ***
 ****************/
void list_playlists(GString* result) {
    int i, n, t;
    sp_playlist* pl;

    if (g_debug)
        fprintf(stderr, "Waiting for container...\n");
    container_ready();

    n = playlists_len();
    if (n == -1) {
        fprintf(stderr, "Could not determine the number of playlists\n");
        return;
    }
    if (g_debug)
        fprintf(stderr, "%d playlists\n", n);

    playlist_lock();
    for (i=0; i<n; i++) {
        pl = playlist_get(i);
        if (!sp_playlist_is_loaded(pl)) continue;
        t = sp_playlist_num_tracks(pl);
        g_string_append_printf(result, "%d %s (%d)\n", i, sp_playlist_name(pl), t);
    }
    playlist_unlock();
}

void list_tracks(int idx, GString* result) {
    sp_playlist* pl;
    sp_track* track;
    GArray* tracks;
    int tracks_nb;
    int i;

    bool track_available;
    int track_min, track_sec;
    const char* track_name;
    GString* track_artist = NULL;
    GString* track_album = NULL;
    GString* track_link = NULL;

    /* Get the playlist */
    pl = playlist_get(idx);
    if (pl == NULL) return;
    
    /* Tracks number */
    tracks_nb = sp_playlist_num_tracks(pl);

    /* If the playlist is empty, just add a newline (an empty string would mean "error") */
    if (tracks_nb == 0) {
        g_string_assign(result, "\n");
        return;
    }

    /* Get the tracks array */
    tracks_lock();
    tracks = tracks_get_playlist(pl);
    if (tracks == NULL) {
        fprintf(stderr, "Can't find tracks array.\n");
        exit(1);
    }

    /* For each track, add a line to the result string */
    for (i=0; i < tracks_nb; i++) {
        track = g_array_index(tracks, sp_track*, i);
        if (!sp_track_is_loaded(track)) continue;

        track_available = sp_track_is_available(track);
        track_get_data(track, &track_name, &track_artist, &track_album, &track_link, &track_min, &track_sec);

        g_string_append_printf(result, "%d%s %s -- \"%s\" -- \"%s\" (%d:%02d) URI:%s\n",
                               i, (track_available ? "" : "-"), track_artist->str,
                               track_album->str, track_name, track_min, track_sec, 
                               track_link->str);
        g_string_free(track_artist, TRUE);
        g_string_free(track_album, TRUE);
        g_string_free(track_link, TRUE);
    }
    tracks_unlock();
}


void play_playlist(int idx, GString* result) {
    g_string_assign(result, "- not implemented\n");
}

void play_track(int pl_idx, int tr_idx, GString* result) {
    sp_playlist* pl;
    sp_track* tr;
    GArray* tracks;
    GString* artist;
    GString* album;
    GString* link;
    const char* name;
    int min, sec;

    /* First get the playlist */
    playlist_lock();
    pl = playlist_get(pl_idx);
    if (pl == NULL) {
        g_string_assign(result, "- invalid playlist\n");
        playlist_unlock();
        return;
    }

    /* Then get the track itself */
    tracks_lock();
    tracks = tracks_get_playlist(pl);
    if (tracks == NULL) {
        fprintf(stderr, "Can't find tracks array.\n");
        exit(1);
    }
    tr = g_array_index(tracks, sp_track*, tr_idx);

    tracks_unlock();
    playlist_unlock();

    /* Load it and play it */
    session_load(tr);
    session_play(1);

    /* Return some data about it to the user */
    track_get_data(tr, &name, &artist, &album, &link, &min, &sec);

    g_string_printf(result, "Artist: %s\n", artist->str);
    g_string_append_printf(result, "Title: %s\n", name);
    g_string_append_printf(result, "Album: %s\n", album->str);
    g_string_append_printf(result, "Duration: %d:%02d\n", min, sec);
    g_string_append_printf(result, "URI: %s\n", link->str);

    g_string_free(artist, TRUE);
    g_string_free(album, TRUE);
    g_string_free(link, TRUE);
}
