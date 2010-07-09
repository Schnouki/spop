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
#include "interface.h"
#include "queue.h"
#include "spotify.h"

/************************
 *** Global variables ***
 ************************/
static GQueue g_queue = G_QUEUE_INIT;
static int g_current_track = -1;
static queue_status g_status = STOPPED;

static gboolean g_repeat = FALSE;


/************************
 *** Queue management ***
 ************************/
void queue_set_track(gboolean notif, sp_track* track) {
    if (g_debug) fprintf(stderr, "Entering queue_set_track()\n");

    sp_track_add_ref(track);
    if (!sp_track_is_loaded(track)) {
        sp_track_release(track);
        if (g_debug) fprintf(stderr, "Track not loaded.\n");
        return;
    }

    if (!sp_track_is_available(track)) {
        sp_track_release(track);
        if (g_debug) fprintf(stderr, "Track is not available.\n");
        return;
    }

    if (g_debug)
        fprintf(stderr, "Setting track %p as queue.\n", track);

    queue_clear(FALSE);
    g_queue_push_tail(&g_queue, track);

    if (notif) queue_notify();
}
void queue_add_track(gboolean notif, sp_track* track) {
    if (g_debug) fprintf(stderr, "Entering queue_add_track()\n");

    sp_track_add_ref(track);
    if (!sp_track_is_loaded(track)) {
        sp_track_release(track);
        if (g_debug) fprintf(stderr, "Track not loaded.\n");
        return;
    }

    if (!sp_track_is_available(track)) {
        sp_track_release(track);
        if (g_debug) fprintf(stderr, "Track is not available.\n");
        return;
    }

    if (g_debug)
        fprintf(stderr, "Adding track %p to queue.\n", track);

    g_queue_push_tail(&g_queue, track);

    if (notif) queue_notify();
}

void queue_set_playlist(gboolean notif, sp_playlist* pl) {
    if (g_debug) fprintf(stderr, "Entering queue_set_playlist()\n");

    GArray* tracks;
    sp_track* track;
    int i;

    if (g_debug)
        fprintf(stderr, "Setting playlist %p as queue.\n", pl);

    tracks = tracks_get_playlist(pl);
    if (!tracks) {
        fprintf(stderr, "Playlist not loaded.\n");
        return;
    }

    queue_clear(FALSE);

    for (i=0; i < tracks->len; i++) {
        track = g_array_index(tracks, sp_track*, i);
        if (sp_track_is_loaded(track) && sp_track_is_available(track))
            g_queue_push_tail(&g_queue, track);
    }
    g_array_free(tracks, TRUE);

    if (notif) queue_notify();
}
void queue_add_playlist(gboolean notif, sp_playlist* pl) {
    if (g_debug) fprintf(stderr, "Entering queue_add_playlist()\n");

    GArray* tracks;
    sp_track* track;
    int i;

    if (g_debug)
        fprintf(stderr, "Adding playlist %p to queue.\n", pl);

    tracks = tracks_get_playlist(pl);
    if (!tracks) {
        fprintf(stderr, "Playlist not loaded.\n");
        return;
    }

    for (i=0; i < tracks->len; i++) {
        track = g_array_index(tracks, sp_track*, i);
        if (sp_track_is_loaded(track) && sp_track_is_available(track))
            g_queue_push_tail(&g_queue, track);
    }
    g_array_free(tracks, TRUE);

    if (notif) queue_notify();
}

void queue_clear(gboolean notif) {
    if (g_debug) fprintf(stderr, "Entering queue_clear()\n");

    queue_stop(FALSE);
    g_queue_foreach(&g_queue, cb_queue_track_release, NULL);
    g_queue_clear(&g_queue);

    if (notif) queue_notify();
}

void queue_remove_tracks(gboolean notif, int idx, int nb) {
    if (g_debug) fprintf(stderr, "Entering queue_remove_tracks()\n");

    int len;
    int i;

    if (g_debug)
        fprintf(stderr, "Removing %d tracks from queue, starting at %d.\n", nb, idx);

    if ((idx < 0) || (nb < 0))
        return;

    len = g_queue_get_length(&g_queue);

    if (idx < len) {
        if (idx + nb >= len)
            nb = len - idx;

        for (i=0; i < nb; i++)
            sp_track_release(g_queue_pop_nth(&g_queue, idx));

        /* Was the current track removed too? */
        if (g_current_track >= idx) {
            if (g_current_track < idx+nb)
                g_current_track = -1;
            else
                g_current_track -= nb;
        }
    }

    if (notif) queue_notify();
}


/***************************
 *** Playback management ***
 ***************************/
void queue_play(gboolean notif) {
    if (g_debug) fprintf(stderr, "Entering queue_play()\n");

    sp_track* track;
    int len;

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
            if (notif) queue_notify();
        }
        else if (g_debug)
            fprintf(stderr, "Nothing to play (empty queue).\n");
        break;

    case PAUSED:
        if (g_debug)
            fprintf(stderr, "Resuming playback.\n");
        session_play(TRUE);
        g_status = PLAYING;
        if (notif) queue_notify();
        break;

    case PLAYING:
        if (g_debug)
            fprintf(stderr, "Already playing: nothing to do.\n");
        break;
    }
}

void queue_stop(gboolean notif) {
    if (g_debug) fprintf(stderr, "Entering queue_stop()\n");

    switch(g_status) {
    case PLAYING:
    case PAUSED:
        if (g_debug)
            fprintf(stderr, "Stopping playback.\n");
        session_unload();
        g_status = STOPPED;
        if (notif) queue_notify();
        break;

    case STOPPED:
        if (g_debug)
            fprintf(stderr, "Already stopped: nothing to do.\n");
        break;
    }
}

void queue_toggle(gboolean notif) {
    if (g_debug) fprintf(stderr, "Entering queue_toggle()\n");

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
        queue_play(FALSE);
    }

    if (notif) queue_notify();
}

void queue_seek(int pos) {
    if (g_debug) fprintf(stderr, "Entering queue_seek()\n");

    sp_track* track;
    int dur;

    switch(g_status) {
    case PLAYING:
    case PAUSED:
        track = g_queue_peek_nth(&g_queue, g_current_track);
        dur = sp_track_duration(track) / 1000;

        if (dur <= 0)
            fprintf(stderr, "Can't get track duration.\n");
        else if ((pos < 0) || ((pos) >= dur))
            fprintf(stderr, "Can't seek: value is out of range.\n");
        else
            session_seek(pos);
        break;
    case STOPPED:
        if (g_debug)
            fprintf(stderr, "Seek: stopped, doing nothing.\n");
    }
}


/***********************************
 *** Information about the queue ***
 ***********************************/
queue_status queue_get_status(sp_track** current_track, int* current_track_number, int* total_tracks) {
    if (g_debug) fprintf(stderr, "Entering queue_get_status()\n");

    if (current_track) {
        if (g_current_track >= 0)
            *current_track = g_queue_peek_nth(&g_queue, g_current_track);
        else
            *current_track = NULL;
    }
    if (current_track_number)
        *current_track_number = g_current_track;
    if (total_tracks)
        *total_tracks = g_queue_get_length(&g_queue);

    return g_status;
}

GArray* queue_tracks() {
    if (g_debug) fprintf(stderr, "Entering queue_tracks()\n");

    GArray* tracks;
    sp_track* tr;
    int i, n;

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

    return tracks;
}


/*********************************************
 *** Notify clients that something changed ***
 *********************************************/
void queue_notify() {
    if (g_debug) fprintf(stderr, "Entering queue_notify()\n");

    interface_notify_idle();
}


/***************************
 *** Move into the queue ***
 ***************************/
void queue_next(gboolean notif) {
    if (g_debug) fprintf(stderr, "Entering queue_next()\n");

    if (g_debug)
        fprintf(stderr, "Switching to next track.\n");

    queue_goto(FALSE, g_current_track + 1);
    if (notif) queue_notify();
}

void queue_prev(gboolean notif) {
    if (g_debug) fprintf(stderr, "Entering queue_prev()\n");

    if (g_debug)
        fprintf(stderr, "Switching to previous track.\n");

    queue_goto(FALSE, g_current_track - 1);
    if (notif) queue_notify();
}

void queue_goto(gboolean notif, int idx) {
    if (g_debug) fprintf(stderr, "Entering queue_goto()\n");

    int len = g_queue_get_length(&g_queue);

    if (idx == g_current_track) {
        if (g_debug)
            fprintf(stderr, "New track == current_track: doing nothing.\n");
        return;
    }

    if (g_debug)
        fprintf(stderr, "Switching to track %d.\n", idx);

    queue_stop(FALSE);

    if (g_repeat) {
        if (idx < 0) idx = len - 1;
        else if (idx >= len) idx = 0;
        if (g_debug)
            fprintf(stderr, "Repeat mode: switching to track %d.\n", idx);
    }

    if (idx < 0) {
        if (g_debug) fprintf(stderr, "Reached beginning of queue, stopping playback.\n");
        g_current_track = -1;
    }
    else if (idx >= len) {
        if (g_debug) fprintf(stderr, "Reached end of queue, stopping playback.\n");
        g_current_track = -1;
    }
    else {
        g_current_track = idx;
        queue_play(FALSE);
    }

    if (notif) queue_notify();
}


/*******************************
 *** Playback mode managment ***
 *******************************/
gboolean queue_get_repeat() {
    return g_repeat;
}
void queue_set_repeat(gboolean notif, gboolean repeat) {
    g_repeat = repeat;
    if (notif) queue_notify();
}


/************************************************************
 *** Callback functions, to be called from a foreach loop ***
 ************************************************************/
void cb_queue_track_release(gpointer data, gpointer user_data) {
    sp_track_release((sp_track*) data);
}
