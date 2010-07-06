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

#include <ao/ao.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "audio.h"

#define BUFSIZE  8192
#define BUFNB    8

typedef struct {
    size_t size;
    char* buf;
} lao_buf;

static gboolean g_ao_init = FALSE;
static int g_ao_driver = -1;
static ao_device* g_ao_dev = NULL;
static size_t g_ao_frame_size;

static GQueue* g_free_bufs = NULL;
static GQueue* g_full_bufs = NULL;
static GMutex* g_buf_mutex = NULL;
static GCond* g_play_cond = NULL;

/* My own perror for libao */
static void lao_perror(const char* s) {
    int ao_err = errno;
    const char* msg;

    switch(ao_err) {
    case AO_ENODRIVER:   msg = "No corresponding driver"; break;
    case AO_ENOTFILE:    msg = "This driver is not a file"; break;
    case AO_ENOTLIVE:    msg = "This driver is not a live output device"; break;
    case AO_EBADOPTION:  msg = "A valid option key has an invalid value"; break;
    case AO_EOPENDEVICE: msg = "Cannot open the device"; break;
    case AO_EOPENFILE:   msg = "Cannot open the file"; break;
    case AO_EFILEEXISTS: msg = "File exists"; break;
    case AO_EBADFORMAT:  msg = "Bad format"; break;
    case AO_EFAIL:
    default:             msg = "Unknown error";
    }

    if (s && (*s != '\0'))
        fprintf(stderr, "%s: %s.\n", s, msg);
    else
        fprintf(stderr, "%s.\n", msg);
}

/* Audio player thread */
static void* lao_player(gpointer data) {
    while (!g_ao_dev) {
        /* Device not ready: wait */
        usleep(10000);
    }
    
    g_mutex_lock(g_buf_mutex);

    while (TRUE) {
        lao_buf* buf = g_queue_pop_head(g_full_bufs);

        if (buf) {
            /* There is something to play */
            g_mutex_unlock(g_buf_mutex);
     
            if (!ao_play(g_ao_dev, buf->buf, buf->size)) {
                fprintf(stderr, "Error while playing sound with libao.\n");
                exit(1);
            }

            g_mutex_lock(g_buf_mutex);
            g_queue_push_tail(g_free_bufs, buf);
        }
        else {
            /* Nothing to play: wait */
            g_cond_wait(g_play_cond, g_buf_mutex);
        }
    }

    return NULL;
}

/* "Private" functions, used to set up the libao device */
static void lao_setup(const sp_audioformat* format) {
    ao_sample_format lao_fmt;

    if (!g_ao_init) {
        GError* err = NULL;
        lao_buf* bufs = NULL;
        int i;

        ao_initialize();
        g_ao_driver = ao_default_driver_id();

        g_free_bufs = g_queue_new();
        if (!g_free_bufs) {
            fprintf(stderr, "Can't allocate queue of free buffers.\n");
            exit(1);
        }
        g_full_bufs = g_queue_new();
        if (!g_full_bufs) {
            fprintf(stderr, "Can't allocate queue of full buffers.\n");
            exit(1);
        }

        bufs = (lao_buf*) malloc(BUFNB*sizeof(lao_buf));
        if (!bufs) {
            fprintf(stderr, "Can't allocate buffer structures.\n");
            exit(1);
        }
        for (i=0; i < BUFNB; i++) {
            bufs[i].buf = malloc(BUFSIZE);
            if (!(bufs[i].buf)) {
                fprintf(stderr, "Can't allocate buffer.\n");
                exit(1);
            }
            g_queue_push_tail(g_free_bufs, &(bufs[i]));
        }

        g_buf_mutex = g_mutex_new();
        g_play_cond = g_cond_new();
        
        if (!g_thread_create(lao_player, NULL, FALSE, &err)) {
            fprintf(stderr, "Error while creating libao player thread: %s\n", err->message);
            exit(1);
        }
        g_ao_init = TRUE;
    }

    /* Set up sample format */
    if (format->sample_type != SP_SAMPLETYPE_INT16_NATIVE_ENDIAN) {
        fprintf(stderr, "Unsupported sample type.\n");
        exit(1);
    }

    lao_fmt.bits = 16;
    lao_fmt.rate = format->sample_rate;
    lao_fmt.channels = format->channels;
    lao_fmt.byte_format = AO_FMT_NATIVE;
    lao_fmt.matrix = NULL;
    g_ao_frame_size = sizeof(int16_t) * format->channels;

    /* Open the device */
    g_ao_dev = ao_open_live(g_ao_driver, &lao_fmt, NULL);
    if (!g_ao_dev) {
        lao_perror("Error while opening libao device");
        exit(1);
    }
}

/* "Public" function, called from a libspotify callback */
int audio_delivery(const sp_audioformat* format, const void* frames, int num_frames) {
    lao_buf* buf;
    size_t size;

    /* What are we supposed to do here? */
    if (num_frames == 0) {
        /* Pause: flush the queue */
        g_mutex_lock(g_buf_mutex);
        while (g_queue_get_length(g_full_bufs) > 0) {
            buf = g_queue_pop_tail(g_full_bufs);
            g_queue_push_tail(g_free_bufs, buf);
        }
        g_mutex_unlock(g_buf_mutex);
        return 0;
    }
    else {
        if (!g_ao_dev)
            lao_setup(format);

        /* Try to add data to the queue */
        g_mutex_lock(g_buf_mutex);

        if (g_queue_get_length(g_free_bufs) == 0) {
            /* Can't add anything */
            g_mutex_unlock(g_buf_mutex);
            return 0;
        }

        /* Copy data to a free buffer */
        buf = g_queue_pop_head(g_free_bufs);
        size = BUFSIZE < (num_frames * g_ao_frame_size) ? BUFSIZE : (num_frames * g_ao_frame_size);
        memcpy(buf->buf, frames, size);
        buf->size = size;

        /* Make the buffer available to the player */
        g_queue_push_tail(g_full_bufs, buf);
        g_cond_signal(g_play_cond);

        g_mutex_unlock(g_buf_mutex);

        return size / g_ao_frame_size;
    }
}
