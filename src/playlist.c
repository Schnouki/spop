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
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "spop.h"
#include "playlist.h"
#include "session.h"

/* Global variables used only from here */
static sp_playlistcontainer* g_container;
static GArray* g_playlists;
static sem_t g_container_loaded_sem;

static sp_playlistcontainer_callbacks g_container_callbacks = {
    &cb_playlist_added,
    &cb_playlist_removed,
    &cb_playlist_moved,
    &cb_container_loaded
};


/* Functions exposed to the rest of spop */
void playlist_init() {
    /* Semaphore used to determine if the playlist container is loaded */
    sem_init(&g_container_loaded_sem, 0, 0);

    /* Init the playlists sequence */
    g_playlists = g_array_new(FALSE, TRUE, sizeof(sp_playlist*));

    /* Get the container */
    g_container = session_playlistcontainer();
    if (g_container == NULL) {
        fprintf(stderr, "Could not get the playlist container\n");
        exit(1);
    }

    /* Callback to be able to wait until it is loaded */
    sp_playlistcontainer_add_callbacks(g_container, &g_container_callbacks, NULL);
}

int playlists_len() {
    return g_playlists->len;
}

sp_playlist* playlist_get(int nb) {
    if ((nb >= 0) && (nb < g_playlists->len))
        return g_array_index(g_playlists, sp_playlist*, nb);
    else
        return NULL;
}


/* Utility functions */
void container_ready() {
    sem_wait(&g_container_loaded_sem);
    sem_post(&g_container_loaded_sem);
}

/* Commands */
void list_playlists(GString* result) {
    int i, n, t;
    sp_playlist* pl;

    if (g_debug)
        fprintf(stderr, "Waiting for container...\n");
    container_ready();

    n = sp_playlistcontainer_num_playlists(g_container);
    if (n == -1) {
        fprintf(stderr, "Could not determine the number of playlists\n");
        return;
    }
    if (g_debug)
        fprintf(stderr, "%d playlists\n", n);

    for (i=0; i<n; i++) {
        pl = g_array_index(g_playlists, sp_playlist*, i);
        if (!sp_playlist_is_loaded(pl)) continue;
        t = sp_playlist_num_tracks(pl);
        g_string_append_printf(result, "%d %s (%d)\n", i, sp_playlist_name(pl), t);
    }
}


/* Callbacks, not to be used directly */
void cb_container_loaded(sp_playlistcontainer* pc, void* data) {
    int i, np;
    sp_playlist* pl;

    np = sp_playlistcontainer_num_playlists(pc);
    if (np == -1) {
        fprintf(stderr, "Could not determine the number of playlists\n");
        exit(1);
    }

    /* Begin loading the playlists */
    g_array_set_size(g_playlists, np);
    for (i=0; i < np; i++) {
        pl = sp_playlistcontainer_playlist(pc, i);
        g_array_insert_val(g_playlists, i, pl);
    }

    sem_post(&g_container_loaded_sem);
}
void cb_playlist_added(sp_playlistcontainer* pc, sp_playlist* playlist, int position, void* userdata) {
    g_array_insert_val(g_playlists, position, playlist);
}
void cb_playlist_removed(sp_playlistcontainer* pc, sp_playlist* playlist, int position, void* userdata) {
    g_array_remove_index(g_playlists, position);
}
void cb_playlist_moved(sp_playlistcontainer* pc, sp_playlist* playlist, int position, int new_position, void* userdata) {
    g_array_remove_index(g_playlists, position);
    g_array_insert_val(g_playlists, position, playlist);
}
