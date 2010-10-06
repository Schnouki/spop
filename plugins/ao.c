/*
 * Copyright (C) 2010 Thomas Jost
 *
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
 *
 * Additional permission under GNU GPL version 3 section 7
 *
 * If you modify this Program, or any covered work, by linking or combining it
 * with libspotify (or a modified version of that library), containing parts
 * covered by the terms of the Libspotify Terms of Use, the licensors of this
 * Program grant you additional permission to convey the resulting work.
 */

#include <ao/ao.h>
#include <glib.h>
#include <gmodule.h>
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

/* My own strerror for libao */
static const char* lao_strerror(void) {
    int ao_err = errno;

    switch(ao_err) {
    case AO_ENODRIVER:   return "No corresponding driver";
    case AO_ENOTFILE:    return "This driver is not a file";
    case AO_ENOTLIVE:    return "This driver is not a live output device";
    case AO_EBADOPTION:  return "A valid option key has an invalid value";
    case AO_EOPENDEVICE: return "Cannot open the device";
    case AO_EOPENFILE:   return "Cannot open the file";
    case AO_EFILEEXISTS: return "File exists";
    case AO_EBADFORMAT:  return "Bad format";
    case AO_EFAIL:
    default:             return "Unknown error";
    }
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
     
            if (!ao_play(g_ao_dev, buf->buf, buf->size))
                g_error("Error while playing sound with libao");

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
        if (!g_free_bufs)
            g_error("Can't allocate a queue of free buffers");
        g_full_bufs = g_queue_new();
        if (!g_full_bufs)
            g_error("Can't allocate a queue of full buffers");

        bufs = (lao_buf*) malloc(BUFNB*sizeof(lao_buf));
        if (!bufs)
            g_error("Can't allocate buffer structures");
        for (i=0; i < BUFNB; i++) {
            bufs[i].buf = malloc(BUFSIZE);
            if (!(bufs[i].buf))
                g_error("Can't allocate buffer");
            g_queue_push_tail(g_free_bufs, &(bufs[i]));
        }

        g_buf_mutex = g_mutex_new();
        g_play_cond = g_cond_new();
        
        if (!g_thread_create(lao_player, NULL, FALSE, &err))
            g_error("Error while creating libao player thread: %s", err->message);
        g_ao_init = TRUE;
    }

    /* Set up sample format */
    if (format->sample_type != SP_SAMPLETYPE_INT16_NATIVE_ENDIAN)
        g_error("Unsupported sample type");

    lao_fmt.bits = 16;
    lao_fmt.rate = format->sample_rate;
    lao_fmt.channels = format->channels;
    lao_fmt.byte_format = AO_FMT_NATIVE;
    lao_fmt.matrix = NULL;
    g_ao_frame_size = sizeof(int16_t) * format->channels;

    /* Open the device */
    g_ao_dev = ao_open_live(g_ao_driver, &lao_fmt, NULL);
    if (!g_ao_dev)
        g_error("Error while opening libao device: %s", lao_strerror());
}

/* "Public" function, called from a libspotify callback */
G_MODULE_EXPORT int audio_delivery(const sp_audioformat* format, const void* frames, int num_frames) {
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
