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
static gboolean g_shuffle = FALSE;
static int g_prime;
static int g_shuffle_first;


/************************
 *** Queue management ***
 ************************/
void queue_set_track(gboolean notif, sp_track* track) {
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
    if (g_shuffle) queue_setup_shuffle();
    g_shuffle_first = -1;

    if (notif) queue_notify();
}
void queue_add_track(gboolean notif, sp_track* track) {
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
    if (g_shuffle) queue_setup_shuffle();
    g_shuffle_first = g_current_track;

    if (notif) queue_notify();
}

void queue_set_playlist(gboolean notif, sp_playlist* pl) {
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

    if (g_shuffle) queue_setup_shuffle();
    g_shuffle_first = -1;

    if (notif) queue_notify();
}
void queue_add_playlist(gboolean notif, sp_playlist* pl) {
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

    if (g_shuffle) queue_setup_shuffle();
    g_shuffle_first = g_current_track;

    if (notif) queue_notify();
}

void queue_clear(gboolean notif) {
    queue_stop(FALSE);
    g_queue_foreach(&g_queue, cb_queue_track_release, NULL);
    g_queue_clear(&g_queue);

    if (notif) queue_notify();
}

void queue_remove_tracks(gboolean notif, int idx, int nb) {
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

    if (g_shuffle) queue_setup_shuffle();
    g_shuffle_first = g_current_track;

    if (notif) queue_notify();
}


/***************************
 *** Playback management ***
 ***************************/
void queue_play(gboolean notif) {
    sp_track* track;
    int len;

    len = g_queue_get_length(&g_queue);

    switch(g_status) {
    case STOPPED:
        if (len > 0) {
            if (g_shuffle && (g_shuffle_first == -1))
                g_shuffle_first = g_current_track = g_random_int_range(0, len);
            else if (g_current_track < 0)
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
    interface_notify_idle();
}


/***************************
 *** Move into the queue ***
 ***************************/
void queue_next(gboolean notif) {
    int n;

    if (g_debug)
        fprintf(stderr, "Switching to next track.\n");

    if (g_shuffle) {
        if (g_current_track == -1)
            n = g_shuffle_first;
        else {
            n = (g_current_track + g_prime) % g_queue_get_length(&g_queue);
            if ((n == g_shuffle_first) && !g_repeat)
                n = -1;
        }
    }
    else
        n = g_current_track + 1;

    queue_goto(FALSE, n, FALSE);
    if (notif) queue_notify();
}

void queue_prev(gboolean notif) {
    int n;

    if (g_debug)
        fprintf(stderr, "Switching to previous track.\n");

    if (g_shuffle) {
        if (g_current_track == -1)
            n = (g_shuffle_first - g_prime) % g_queue_get_length(&g_queue);
        else {
            if ((g_current_track == g_shuffle_first) && !g_repeat)
                n = -1;
            else
                n = (g_current_track - g_prime) % g_queue_get_length(&g_queue);
        }
    }
    else
        n = g_current_track - 1;

    queue_goto(FALSE, n, FALSE);
    if (notif) queue_notify();
}

void queue_goto(gboolean notif, int idx, gboolean reset_shuffle_first) {
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
        if (reset_shuffle_first)
            g_shuffle_first = idx;
        queue_play(FALSE);
    }

    if (notif) queue_notify();
}


/*******************************
 *** Playback mode managment ***
 *******************************/
gboolean queue_get_shuffle() {
    return g_shuffle;
}
void queue_set_shuffle(gboolean notif, gboolean shuffle) {
    g_shuffle = shuffle;
    if (g_shuffle) queue_setup_shuffle();
    g_shuffle_first = g_current_track;
    if (notif) queue_notify();
}

gboolean is_prime(int n) {
    int i;

    if (n <= 1) return FALSE;
    else if (n <= 100) {
        switch (n) {
        case 2: case 3: case 5: case 7: case 11: case 13: case 17: case 19:
        case 23: case 29: case 31: case 37: case 41: case 43: case 47:
        case 53: case 59: case 61: case 67: case 71: case 73: case 79:
        case 83: case 89: case 97: return TRUE;
        default: return FALSE;
        }
    }
    else {
        if ((n % 2) == 0) return FALSE;
        for (i=3; i*i <= n; i += 2)
            if ((n%i) == 0)
                return FALSE;
        return TRUE;
    }
}
void queue_setup_shuffle() {
    /* Use the method described on http://en.wikipedia.org/wiki/Full_cycle */
    int len = g_queue_get_length(&g_queue);
    int incr = -1;
    int n, r;

    if (len == 0) return;

    if (g_debug)
        fprintf(stderr, "Finding new shuffle parameters...\n");
    
    if (len <= 3) {
        n = 5;
    }

    r = g_random_int_range(3, len);
    n = r;
    do {
        n += incr;
        if (n == 2) {
            n = r;
            incr = 1;
        }
        while (!is_prime(n)) {
            n += incr;
        }
    } while ((len % n) == 0);

    if (g_debug)
        fprintf(stderr, "New shuffle parameters: len=%d, prime=%d.\n", len, n);

    g_prime = n;
}

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
