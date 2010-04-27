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
#include <stdio.h>
#include <stdlib.h>
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
void tracks_add_playlist(sp_playlist* pl) {
    GArray* tracks;
    int nb;

    /* Number of tracks */
    nb = sp_playlist_num_tracks(pl);
    
    /* Get or create array */
    tracks = g_hash_table_lookup(g_playlist_tracks, pl);
    if (tracks == NULL) {
        tracks = g_array_sized_new(FALSE, TRUE, sizeof(sp_track*), nb);
        g_hash_table_insert(g_playlist_tracks, pl, tracks);
    }
}
void tracks_remove_playlist(sp_playlist* pl) {
    g_hash_table_remove(g_playlist_tracks, pl);
}

GString* track_get_link(sp_track* track) {
    sp_link* link;
    GString* uri;

    link = sp_link_create_from_track(track, 0);
    if (!link) {
        fprintf(stderr, "Can't get URI from track\n");
        exit(1);
    }
    uri = g_string_sized_new(1024);
    if (sp_link_as_string(link, uri->str, 1024) < 0) {
        fprintf(stderr, "Can't render URI from link\n");
        exit(1);
    }
    sp_link_release(link);

    return uri;
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
    GString* track_link;

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
    tracks = g_hash_table_lookup(g_playlist_tracks, pl);
    if (tracks == NULL) {
        fprintf(stderr, "Can't find tracks array.\n");
        exit(1);
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
        track_link = track_get_link(track);

        g_string_append_printf(result, "%d%s %s -- \"%s\" -- \"%s\" (%d:%02d) URI:%s\n",
                               i, (track_available ? "" : "-"), track_artist->str,
                               track_album, track_name, track_duration/60000, 
                               (track_duration/1000)%60, track_link->str);
        g_string_free(track_artist, TRUE);
        g_string_free(track_link, TRUE);
    }
}


/* Callbacks, not to be used directly */
void cb_tracks_added(sp_playlist* pl, sp_track* const* tracks, int num_tracks, int position, void* userdata) {
    GArray* ta;
    int i;

    /* Get tracks array */
    ta = g_hash_table_lookup(g_playlist_tracks, pl);
    if (ta == NULL) {
        fprintf(stderr, "Can't find tracks array\n");
        exit(1);
    }

    /* Insert tracks in array */
    for (i=0; i < num_tracks; i++)
        g_array_insert_val(ta, position+i, tracks[i]);

    if (g_debug)
        fprintf(stderr, "Added %d tracks at position %d.\n", num_tracks, position);
}
void cb_tracks_removed(sp_playlist* pl, const int* tracks, int num_tracks, void* userdata) {
    GArray* ta;
    int i;

    /* Get tracks array */
    ta = g_hash_table_lookup(g_playlist_tracks, pl);
    if (ta == NULL) {
        fprintf(stderr, "Can't find tracks array\n");
        exit(1);
    }

    /* Remove tracks from array */
    for (i=num_tracks-1; i >= 0; i--)
        g_array_remove_index(ta, tracks[i]);

    if (g_debug)
        fprintf(stderr, "Removed %d tracks.\n", num_tracks);
}
void cb_tracks_moved(sp_playlist* pl, const int* tracks, int num_tracks, int new_position, void* userdata) {
    GArray* ta;
    GArray* tmp;
    sp_track* track;
    int i;

    /* Get tracks array */
    ta = g_hash_table_lookup(g_playlist_tracks, pl);
    if (ta == NULL) {
        fprintf(stderr, "Can't find tracks array\n");
        exit(1);
    }

    /* Array of tracks to be moved */
    tmp = g_array_sized_new(FALSE, TRUE, sizeof(sp_track*), num_tracks);
    for (i=0; i < num_tracks; i++) {
        track = g_array_index(ta, sp_track*, tracks[i]);
        g_array_insert_val(tmp, i, track);
    }

    /* Insert tracks at their new position */
    g_array_insert_vals(ta, new_position, tmp->data, num_tracks);

    /* Remove tracks from tracks array */
    for (i=num_tracks-1; i >= 0; i--)
        g_array_remove_index(ta, num_tracks + tracks[i]);

    /* Free tmp array */
    g_array_free(tmp, TRUE);

    if (g_debug)
        fprintf(stderr, "Moved %d tracks to position %d.\n", num_tracks, new_position);
}
