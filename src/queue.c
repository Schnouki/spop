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

#include "spop.h"
#include "queue.h"
#include "spotify.h"

/************************
 *** Global variables ***
 ************************/
static GQueue g_queue = G_QUEUE_INIT;
static GStaticRWLock g_queue_lock = G_STATIC_RW_LOCK_INIT;
static int g_current_track = -1;
static queue_status g_status = STOPPED;


/**********************************
 *** Queue management functions ***
 **********************************/
void queue_set_track(sp_track* track) {
    if (g_debug)
        fprintf(stderr, "Setting track %p as queue.\n", track);

    g_static_rw_lock_writer_lock(&g_queue_lock);

    session_unload();
    g_status = STOPPED;
    g_queue_clear(&g_queue);
    g_current_track = -1;

    g_queue_push_tail(&g_queue, track);

    g_static_rw_lock_writer_unlock(&g_queue_lock);
}
void queue_add_track(sp_track* track) {
    if (g_debug)
        fprintf(stderr, "Adding track %p to queue.\n", track);

    g_static_rw_lock_writer_lock(&g_queue_lock);
    g_queue_push_tail(&g_queue, track);
    g_static_rw_lock_writer_unlock(&g_queue_lock);
}

void queue_set_playlist(sp_playlist* pl) {
    GArray* tracks;
    int i;

    if (g_debug)
        fprintf(stderr, "Setting playlist %p as queue.\n", pl);

    tracks = tracks_get_playlist(pl);
    if (!tracks) {
        fprintf(stderr, "Playlist not loaded.\n");
        exit(1);
    }

    g_static_rw_lock_writer_lock(&g_queue_lock);

    session_unload();
    g_status = STOPPED;
    g_queue_clear(&g_queue);
    g_current_track = -1;

    for (i=0; i < tracks->len; i++)
        g_queue_push_tail(&g_queue, g_array_index(tracks, sp_track*, i));
    g_array_free(tracks, TRUE);

    g_static_rw_lock_writer_unlock(&g_queue_lock);
}
void queue_add_playlist(sp_playlist* pl) {
    GArray* tracks;
    int i;

    if (g_debug)
        fprintf(stderr, "Adding playlist %p to queue.\n", pl);

    tracks = tracks_get_playlist(pl);
    if (!tracks) {
        fprintf(stderr, "Playlist not loaded.\n");
        exit(1);
    }

    g_static_rw_lock_writer_lock(&g_queue_lock);

    for (i=0; i < tracks->len; i++)
        g_queue_push_tail(&g_queue, g_array_index(tracks, sp_track*, i));
    g_array_free(tracks, TRUE);

    g_static_rw_lock_writer_unlock(&g_queue_lock);
}

void queue_remove_tracks(int idx, int nb) {
    int len;
    int i;

    if (g_debug)
        fprintf(stderr, "Removing %d tracks from queue, starting at %d.\n", nb, idx);

    if ((idx < 0) || (nb < 0))
        return;

    g_static_rw_lock_writer_lock(&g_queue_lock);
    len = g_queue_get_length(&g_queue);

    if (idx < len) {
        if (idx + nb >= len)
            nb = len - idx;

        for (i=0; i < nb; i++)
            g_queue_pop_nth(&g_queue, idx);

        /* Was the current track removed too? */
        if (g_current_track >= idx) {
            if (g_current_track < idx+nb)
                g_current_track = -1;
            else
                g_current_track -= nb;
        }
    }

    g_static_rw_lock_writer_unlock(&g_queue_lock);
}


/******************************
 *** Play status management ***
 ******************************/
void queue_play() {
    sp_track* track;
    int len;

    g_static_rw_lock_writer_lock(&g_queue_lock);
    len = g_queue_get_length(&g_queue);

    switch(g_status) {
    case STOPPED:
        if (len > 0) {
            if (g_current_track < 0)
                g_current_track = 0;
            else if (g_current_track >= len)
                g_current_track = len-1;

            if (g_debug)
                fprintf(stderr, "Playing track %d.\n", g_current_track);

            track = g_queue_peek_nth(&g_queue, g_current_track);
            if (!track) {
                fprintf(stderr, "Can't peek track.\n");
                exit(1);
            }

            session_load(track);
            session_play(TRUE);
            g_status = PLAYING;
        }
        else if (g_debug)
            fprintf(stderr, "Nothing to play (empty queue).\n");
        break;

    case PAUSED:
        if (g_debug)
            fprintf(stderr, "Resuming playback.\n");
        session_play(TRUE);
        g_status = PLAYING;
        break;

    case PLAYING:
        if (g_debug)
            fprintf(stderr, "Already playing: nothing to do.\n");
        break;
    }

    g_static_rw_lock_writer_unlock(&g_queue_lock);
}

void queue_stop() {
    g_static_rw_lock_writer_lock(&g_queue_lock);

    switch(g_status) {
    case PLAYING:
    case PAUSED:
        if (g_debug)
            fprintf(stderr, "Stopping playback.\n");
        session_unload();
        g_status = STOPPED;
        break;

    case STOPPED:
        if (g_debug)
            fprintf(stderr, "Already stopped: nothing to do.\n");
        break;
    }

    g_static_rw_lock_writer_unlock(&g_queue_lock);
}

void queue_toggle() {
    g_static_rw_lock_writer_lock(&g_queue_lock);

    switch(g_status) {
    case PLAYING:
        if (g_debug)
            fprintf(stderr, "Toggle: now paused.\n");
        session_play(FALSE);
        g_status = PAUSED;
        break;

    case PAUSED:
        if (g_debug)
            fprintf(stderr, "Toggle: now playing.\n");
        session_play(TRUE);
        g_status = PLAYING;
        break;

    case STOPPED:
        if (g_debug)
            fprintf(stderr, "Toggle: was stopped, will now start playing.\n");
        g_static_rw_lock_writer_unlock(&g_queue_lock);
        queue_play();
        return;
    }

    g_static_rw_lock_writer_unlock(&g_queue_lock);
}

queue_status queue_get_status(sp_track** current_track, int* current_track_number, int* total_tracks) {
    queue_status s;

    g_static_rw_lock_reader_lock(&g_queue_lock);

    if (current_track) {
        *current_track = NULL;
        if (g_current_track >= 0)
            *current_track = g_queue_peek_nth(&g_queue, g_current_track);
    }
    if (current_track_number)
        *current_track_number = g_current_track;
    if (total_tracks)
        *total_tracks = g_queue_get_length(&g_queue);

    s = g_status;

    g_static_rw_lock_reader_unlock(&g_queue_lock);

    return s;
}

GArray* queue_tracks() {
    GArray* tracks;
    sp_track* tr;
    int i, n;

    g_static_rw_lock_reader_lock(&g_queue_lock);

    n = g_queue_get_length(&g_queue);
    tracks = g_array_sized_new(FALSE, FALSE, sizeof(sp_track*), n);
    if (!tracks) {
        fprintf(stderr, "Can't allocate array of %d tracks.\n", n);
        exit(1);
    }

    for (i=0; i < n; i++) {
        tr = g_queue_peek_nth(&g_queue, i);
        g_array_append_val(tracks, tr);
    }

    g_static_rw_lock_reader_unlock(&g_queue_lock);

    return tracks;
}

void queue_next() {
    g_static_rw_lock_writer_lock(&g_queue_lock);

    if (g_debug)
        fprintf(stderr, "Switching to next track.\n");

    g_current_track += 1;
    if (g_current_track >= g_queue_get_length(&g_queue)) {
        if (g_debug)
            fprintf(stderr, "Last track reached, stopping playback.\n");
        g_current_track = -1;
        g_status = STOPPED;
    }

    session_unload();
    if (g_status != STOPPED) {
        session_load(g_queue_peek_nth(&g_queue, g_current_track));
        if (g_status == PLAYING)
            session_play(TRUE);
    }

    g_static_rw_lock_writer_unlock(&g_queue_lock);
}

void queue_prev() {
    g_static_rw_lock_writer_lock(&g_queue_lock);

    if (g_debug)
        fprintf(stderr, "Switching to previous track.\n");

    if (g_current_track == 0) {
        if (g_debug)
            fprintf(stderr, "First track reached, stopping playback.\n");
        g_current_track = -1;
        g_status = STOPPED;
    }
    else if (g_current_track > 0)
        g_current_track -= 1;

    session_unload();
    if (g_status != STOPPED) {
        session_load(g_queue_peek_nth(&g_queue, g_current_track));
        if (g_status == PLAYING)
            session_play(TRUE);
    }

    g_static_rw_lock_writer_unlock(&g_queue_lock);
}

void queue_set(int idx) {
    g_static_rw_lock_writer_lock(&g_queue_lock);

    if (g_debug)
        fprintf(stderr, "Switching to track %d.\n", idx);

    if ((idx >= 0) && (idx < g_queue_get_length(&g_queue))) {
        g_current_track = idx;
    }
    else {
        if (g_debug)
            fprintf(stderr, "Invalid track number, stopping playback.\n");
        g_current_track = -1;
        g_status = STOPPED;
    }

    session_unload();
    if (g_status != STOPPED) {
        session_load(g_queue_peek_nth(&g_queue, g_current_track));
        if (g_status == PLAYING)
            session_play(TRUE);
    }

    g_static_rw_lock_writer_unlock(&g_queue_lock);
}
