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

#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "spop.h"
#include "playlist.h"
#include "session.h"

/* Global variables used only from here */
static sp_playlistcontainer* g_container;
static sem_t g_container_loaded_sem;

static sp_playlistcontainer_callbacks g_container_callbacks = {
    NULL,
    NULL,
    NULL,
    &cb_container_loaded
};


/* Functions exposed to the rest of spop */
void playlist_init() {
    /* Semaphore used to determine if the playlist container is loaded */
    sem_init(&g_container_loaded_sem, 0, 0);

    /* Get the container */
    g_container = session_playlistcontainer();
    if (g_container == NULL) {
        fprintf(stderr, "Could not get the playlist container\n");
        exit(1);
    }

    /* Callback to be able to wait until it is loaded */
    sp_playlistcontainer_add_callbacks(g_container, &g_container_callbacks, NULL);
}

/* Utility functions */
void container_ready() {
    sem_wait(&g_container_loaded_sem);
    sem_post(&g_container_loaded_sem);
}

/* Commands */
void list_playlists() {
    int i, n, t;
    sp_playlist* pl;

    printf("Waiting for container...\n");
    container_ready();

    n = sp_playlistcontainer_num_playlists(g_container);
    if (n == -1) {
        fprintf(stderr, "Could not determine the number of playlists\n");
        return;
    }
    printf("%d playlists\n", n);

    for (i=0; i<n; i++) {
        pl = sp_playlistcontainer_playlist(g_container, i);
        while (!sp_playlist_is_loaded(pl)) { usleep(10000); }
        t = sp_playlist_num_tracks(pl);
        printf("Playlist %d: \"%s\", %d tracks\n", i+1, sp_playlist_name(pl), t);
    }
}


/* Callbacks, not to be used directly */
void cb_container_loaded(sp_playlistcontainer* pc, void* data) {
    sem_post(&g_container_loaded_sem);
}
