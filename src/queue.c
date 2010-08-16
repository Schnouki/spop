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
static GQueue g_shuffle_queue = G_QUEUE_INIT;
static int g_shuffle_first;


/************************
 *** Queue management ***
 ************************/
void queue_set_track(gboolean notif, sp_track* track) {
    sp_track_add_ref(track);
    if (!sp_track_is_loaded(track)) {
        sp_track_release(track);
        g_debug("Track not loaded.");
        return;
    }

    if (!sp_track_is_available(track)) {
        sp_track_release(track);
        g_debug("Track is not available.");
        return;
    }

    g_debug("Setting track %p as queue.", track);

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
        g_debug("Track not loaded.");
        return;
    }

    if (!sp_track_is_available(track)) {
        sp_track_release(track);
        g_debug("Track is not available.");
        return;
    }

    g_debug("Adding track %p to queue.", track);

    g_queue_push_tail(&g_queue, track);
    if (g_shuffle) queue_setup_shuffle();
    g_shuffle_first = g_current_track;

    if (notif) queue_notify();
}

void queue_set_playlist(gboolean notif, sp_playlist* pl) {
    GArray* tracks;
    sp_track* track;
    int i;

    g_debug("Setting playlist %p as queue.", pl);

    tracks = tracks_get_playlist(pl);
    if (!tracks) {
        g_info("Playlist not loaded.");
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

    g_debug("Adding playlist %p to queue.", pl);

    tracks = tracks_get_playlist(pl);
    if (!tracks) {
        g_info("Playlist not loaded.");
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
    g_current_track = -1;

    if (notif) queue_notify();
}

void queue_remove_tracks(gboolean notif, int idx, int nb) {
    int len;
    int i;

    g_debug("Removing %d tracks from queue, starting at %d.", nb, idx);

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
            if (g_current_track < idx+nb) {
                queue_stop(FALSE);
                g_current_track = -1;
            }
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

            g_debug("Playing track %d.", g_current_track);

            track = g_queue_peek_nth(&g_queue, g_current_track);
            if (!track)
                g_error("Can't peek track.");

            session_load(track);
            session_play(TRUE);
            g_status = PLAYING;
            if (notif) queue_notify();
        }
        else g_debug("Nothing to play (empty queue).");
        break;

    case PAUSED:
        g_debug("Resuming playback.");
        session_play(TRUE);
        g_status = PLAYING;
        if (notif) queue_notify();
        break;

    case PLAYING:
        g_debug("Already playing: nothing to do.");
        break;
    }
}

void queue_stop(gboolean notif) {
    switch(g_status) {
    case PLAYING:
    case PAUSED:
        g_debug("Stopping playback.");
        session_unload();
        g_status = STOPPED;
        if (notif) queue_notify();
        break;

    case STOPPED:
        g_debug("Already stopped: nothing to do.");
        break;
    }
}

void queue_toggle(gboolean notif) {
    switch(g_status) {
    case PLAYING:
        g_debug("Toggle: now paused.");
        session_play(FALSE);
        g_status = PAUSED;
        break;

    case PAUSED:
        g_debug("Toggle: now playing.");
        session_play(TRUE);
        g_status = PLAYING;
        break;

    case STOPPED:
        g_debug("Toggle: was stopped, will now start playing.");
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
            g_warning("Can't get track duration.");
        else if ((pos < 0) || ((pos) >= dur))
            g_info("Can't seek: value is out of range.");
        else
            session_seek(pos);
        break;
    case STOPPED:
        g_debug("Seek: stopped, doing nothing.");
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
    if (!tracks)
        g_error("Can't allocate array of %d tracks.", n);

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
    int n, p;
    int len = g_queue_get_length(&g_queue);

    g_debug("Switching to next track.");

    if (g_shuffle) {
        /* Possible cases: g_repeat, g_current_track == -1, g_shuffle_first == -1 */
        if (g_current_track == -1) {
            if (g_shuffle_first == -1) {
                /* Pick a random track */
                n = g_random_int_range(0, len);
                g_shuffle_first = n;
            }
            else {
                n = g_shuffle_first;
            }
        }
        else {
            if (g_shuffle_first == -1) {
                g_warning("g_shuffle_first == -1 in goto_next()");
                g_shuffle_first = g_current_track;
            }

            /* Find the index of the current track in the shuffle queue */
            p = g_queue_index(&g_shuffle_queue, GINT_TO_POINTER(g_current_track));
            if (p == -1) g_error("Can't find current track in shuffle queue");

            /* Find the next track in the shuffle queue */
            p = (p+1) % len;
            n = GPOINTER_TO_INT(g_queue_peek_nth(&g_shuffle_queue, p));

            if ((n == g_shuffle_first) && !g_repeat)
                n = -1;
        }
    }
    else {
        n = g_current_track + 1;
        if (g_repeat)
            n %= len;
    }

    queue_goto(FALSE, n, FALSE);
    if (notif) queue_notify();
}

void queue_prev(gboolean notif) {
    int n, p;
    int len = g_queue_get_length(&g_queue);

    g_debug("Switching to previous track.");

    if (g_shuffle) {
        /* Possible cases: g_repeat, g_current_track == -1, g_shuffle_first == -1 */
        if (g_current_track == -1) {
            if (g_shuffle_first == -1) {
                /* Pick a random track */
                n = g_random_int_range(0, len);

                /* Set the next one to be the first shuffle track... */
                p = g_queue_index(&g_shuffle_queue, GINT_TO_POINTER(n));
                if (p == -1) g_error("Can't find last track in shuffle queue");
                p = (p+1) % len;
                g_shuffle_first = GPOINTER_TO_INT(g_queue_peek_nth(&g_shuffle_queue, p));
            }
            else {
                /* Find the track that comes just before the first shuffle track */
                p = g_queue_index(&g_shuffle_queue, GINT_TO_POINTER(g_shuffle_first));
                if (p == -1) g_error("Can't find first track in shuffle queue");
                p = (p-1) % len;
                n = GPOINTER_TO_INT(g_queue_peek_nth(&g_shuffle_queue, p));
            }
        }
        else {
            if (g_shuffle_first == -1) {
                g_warning("g_shuffle_first == -1 in goto_prev()");
                g_shuffle_first = g_current_track;
            }
            
            /* Is this the first track in non-repeat mode? */
            if ((g_current_track == g_shuffle_first) && !g_repeat) {
                n = -1;
            }
            else {
                /* Find the index of the current track in the shuffle queue */
                p = g_queue_index(&g_shuffle_queue, GINT_TO_POINTER(g_current_track));
                if (p == -1)
                    g_error("Can't find current track in shufflequeue");
                
                /* Find the previous track in the shuffle queue */
                p = (p+len-1) % len;
                n = GPOINTER_TO_INT(g_queue_peek_nth(&g_shuffle_queue, p));
            }
        }
    }
    else {
        n = g_current_track - 1;
        if (g_repeat)
            n %= len;
    }

    queue_goto(FALSE, n, FALSE);
    if (notif) queue_notify();
}

void queue_goto(gboolean notif, int idx, gboolean reset_shuffle_first) {
    int len = g_queue_get_length(&g_queue);
    queue_status s = g_status;

    if (idx == g_current_track) {
        g_debug("New track == current_track: doing nothing.");
        return;
    }

    queue_stop(FALSE);

    if (idx < 0) {
        g_debug("Reached beginning of queue, stopping playback.");
        g_current_track = -1;
    }
    else if (idx >= len) {
        g_debug("Reached end of queue, stopping playback.");
        g_current_track = -1;
    }
    else {
        g_debug("Switching to track %d.", idx);
        g_current_track = idx;
        if (reset_shuffle_first)
            g_shuffle_first = idx;
        if (s == PAUSED) {
            sp_track* track;
            track = g_queue_peek_nth(&g_queue, g_current_track);
            if (!track)
                g_error("Can't peek track.");
            session_load(track);
            g_status = PAUSED;
        }
        else
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

gint queue_cmp_random(gconstpointer a, gconstpointer b, gpointer user_data) {
    return g_random_boolean() ? 1 : -1;
}
void queue_setup_shuffle() {
    int len = g_queue_get_length(&g_queue);
    int i;

    if (len == 0) return;

    g_debug("Setting up shuffle mode");

    /* Fill the shuffle queue with sequential integers */
    g_queue_clear(&g_shuffle_queue);
    for (i=0; i < len; i++)
        g_queue_push_tail(&g_shuffle_queue, GINT_TO_POINTER(i));

    /* Now randomize the order of its elements */
    g_queue_sort(&g_shuffle_queue, queue_cmp_random, NULL);    
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
