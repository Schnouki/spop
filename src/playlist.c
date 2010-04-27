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
#include "track.h"

/* Global variables used only from here */
static sp_playlistcontainer* g_container;
static GArray* g_playlists;
static GStaticRWLock g_playlist_lock = G_STATIC_RW_LOCK_INIT;
static sem_t g_container_loaded_sem;

static sp_playlistcontainer_callbacks g_container_callbacks = {
    &cb_playlist_added,
    &cb_playlist_removed,
    &cb_playlist_moved,
    &cb_container_loaded
};
static sp_playlist_callbacks g_playlist_callbacks = {
    &cb_tracks_added,
    &cb_tracks_removed,
    &cb_tracks_moved,
    NULL,
    NULL,
    NULL,
    NULL
};


/* Functions exposed to the rest of spop */
void playlist_init() {
    /* Semaphore used to determine if the playlist container is loaded */
    sem_init(&g_container_loaded_sem, 0, 0);

    /* Init the playlists sequence */
    g_static_rw_lock_writer_lock(&g_playlist_lock);
    g_playlists = g_array_new(FALSE, TRUE, sizeof(sp_playlist*));
    g_static_rw_lock_writer_unlock(&g_playlist_lock);

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
    int len;
    g_static_rw_lock_reader_lock(&g_playlist_lock);
    len = g_playlists->len;
    g_static_rw_lock_reader_unlock(&g_playlist_lock);

    return len;
}

sp_playlist* playlist_get(int nb) {
    sp_playlist* pl = NULL;

    g_static_rw_lock_reader_lock(&g_playlist_lock);
    if ((nb >= 0) && (nb < g_playlists->len))
        pl = g_array_index(g_playlists, sp_playlist*, nb);
    g_static_rw_lock_reader_unlock(&g_playlist_lock);

    return pl;
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

    g_static_rw_lock_reader_lock(&g_playlist_lock);
    for (i=0; i<n; i++) {
        pl = g_array_index(g_playlists, sp_playlist*, i);
        if (!sp_playlist_is_loaded(pl)) continue;
        t = sp_playlist_num_tracks(pl);
        g_string_append_printf(result, "%d %s (%d)\n", i, sp_playlist_name(pl), t);
    }
    g_static_rw_lock_reader_unlock(&g_playlist_lock);
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
    g_static_rw_lock_writer_lock(&g_playlist_lock);
    g_array_set_size(g_playlists, np);
    for (i=0; i < np; i++) {
        pl = sp_playlistcontainer_playlist(pc, i);
        g_array_insert_val(g_playlists, i, pl);
    }
    g_static_rw_lock_writer_unlock(&g_playlist_lock);

    sem_post(&g_container_loaded_sem);
}
void cb_playlist_added(sp_playlistcontainer* pc, sp_playlist* playlist, int position, void* userdata) {
    if (g_debug)
        fprintf(stderr, "Adding playlist %d.\n", position);

    g_static_rw_lock_writer_lock(&g_playlist_lock);
    g_array_insert_val(g_playlists, position, playlist);
    g_static_rw_lock_writer_unlock(&g_playlist_lock);
    tracks_add_playlist(playlist);
    sp_playlist_add_callbacks(playlist, &g_playlist_callbacks, NULL);
}
void cb_playlist_removed(sp_playlistcontainer* pc, sp_playlist* playlist, int position, void* userdata) {
    if (g_debug)
        fprintf(stderr, "Removing playlist %d.\n", position);

    g_static_rw_lock_writer_lock(&g_playlist_lock);
    g_array_remove_index(g_playlists, position);
    g_static_rw_lock_writer_unlock(&g_playlist_lock);
    tracks_remove_playlist(playlist);
}
void cb_playlist_moved(sp_playlistcontainer* pc, sp_playlist* playlist, int position, int new_position, void* userdata) {
    if (g_debug)
        fprintf(stderr, "Moving playlist %d to %d.\n", position, new_position);

    g_static_rw_lock_writer_lock(&g_playlist_lock);
    g_array_remove_index(g_playlists, position);
    g_array_insert_val(g_playlists, position, playlist);
    g_static_rw_lock_writer_unlock(&g_playlist_lock);
}
