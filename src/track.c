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
#include <unistd.h>

#include "spop.h"
#include "playlist.h"
#include "track.h"

/* Global variables used only from here */
static GHashTable* g_playlist_tracks;

/* Functions exposed to the rest of spop */
void tracks_init() {
    g_playlist_tracks = g_hash_table_new(NULL, NULL);
}

/* Utility functions */

/* Commands */
void list_tracks(int idx, GString* result) {
    sp_playlist* pl;
    sp_track* track;
    GArray* tracks;
    int tracks_nb;
    int i, j;

    sp_album* album;
    sp_artist* artist;
    bool track_available;
    int track_duration;
    const char* track_name;
    GString* track_artist = NULL;
    const char* track_album;

    /* Get the playlist */
    pl = playlist_get(idx);
    if (pl == NULL) return;
    
    /* Tracks number */
    tracks_nb = sp_playlist_num_tracks(pl);

    /* Get the tracks array */
    tracks = g_hash_table_lookup(g_playlist_tracks, pl);
    if (tracks == NULL) {
        /* Create a new array and populate it */
        tracks = g_array_sized_new(FALSE, TRUE, sizeof(sp_track*), tracks_nb);
        g_hash_table_insert(g_playlist_tracks, pl, tracks);
        for (i=0; i < tracks_nb; i++) {
            track = sp_playlist_track(pl, i);
            g_array_insert_val(tracks, i, track);
        }
    }

    /* For each track, add a line to the result string */
    for (i=0; i < tracks_nb; i++) {
        track = g_array_index(tracks, sp_track*, i);
        if (!sp_track_is_loaded(track)) continue;

        track_available = sp_track_is_available(track);
        track_duration = sp_track_duration(track);
        track_name = sp_track_name(track);
        for (j=0; j < sp_track_num_artists(track); j++) {
            artist = sp_track_artist(track, j);
            while (!sp_artist_is_loaded(artist)) { usleep(10000); }
            if (j == 0) {
                track_artist = g_string_new(sp_artist_name(artist));
            }
            else {
                g_string_append(track_artist, ", ");
                g_string_append(track_artist, sp_artist_name(artist));
            }
        }
        album = sp_track_album(track);
        while (!sp_album_is_loaded(album)) { usleep(10000); }
        track_album = sp_album_name(album);

        g_string_append_printf(result, "%d%s %s -- \"%s\" -- \"%s\" (%d:%02d)\n",
                               i, (track_available ? "" : "-"), track_artist->str,
                               track_album, track_name,
                               track_duration/60000, (track_duration/1000)%60);
        g_string_free(track_artist, TRUE);
    }
}

/* Callbacks, not to be used directly */
