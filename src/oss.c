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

#include <fcntl.h>
#include <poll.h>
#include <sys/soundcard.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "audio.h"

static int g_oss_fd = -1;
static struct pollfd g_pfd;
static size_t g_oss_frame_size;

/* "Private" functions, used to set up the OSS device */
static void oss_open() {
    /* Open the device */
    g_oss_fd = open("/dev/dsp", O_WRONLY);
    if (g_oss_fd == -1) {
        perror("Can't open OSS device");
        exit(1);
    }

    /* Populate the pollfd struct used by poll() */
    g_pfd.fd = g_oss_fd;
    g_pfd.events = POLLOUT;
}

static void oss_close() {
    if (close(g_oss_fd) == -1) {
        perror("Can't close OSS device");
        exit(1);
    }
    g_oss_fd = -1;
}

/* Set OSS parameters using "format" from libspotify */
static void oss_setup(const sp_audioformat* format) {
    int tmp;

    /* sample_type is an enum */
    int sample_type;
    switch (format->sample_type) {
    case SP_SAMPLETYPE_INT16_NATIVE_ENDIAN:
        sample_type = AFMT_S16_NE;
        g_oss_frame_size = sizeof(int16_t) * format->channels;
        break;
    default:
        fprintf(stderr, "Unknown sample type");
        exit(1);
    }

    /* Now really setup the device. The order of the initialization is the one
       suggested in the OSS doc for some old devices, just in case...
       (http://manuals.opensound.com/developer/callorder.html) */

    tmp = format->channels;
    if (ioctl(g_oss_fd, SNDCTL_DSP_CHANNELS, &tmp) == -1) {
        perror("Error setting OSS channels");
        exit(1);
    }
    if (tmp != format->channels) {
        fprintf(stderr, "Could not set OSS channels to %d (set to %d instead)\n", format->channels, tmp);
        exit(1);
    }

    tmp = sample_type;
    if (ioctl(g_oss_fd, SNDCTL_DSP_SETFMT, &tmp) == -1) {
        perror("Error setting OSS sample type");
        exit(1);
    }
    if (tmp != sample_type) {
        fprintf(stderr, "Could not set OSS sample type to %d (set to %d instead)\n", sample_type, tmp);
        exit(1);
    }

    tmp = format->sample_rate;
    if (ioctl(g_oss_fd, SNDCTL_DSP_SPEED, &tmp) == -1) {
        perror("Error setting OSS sample rate");
        exit(1);
    }
    /* Sample rate: the OSS doc that differences up to 10% should be accepted */
    if (((100*abs(format->sample_rate - tmp))/format->sample_rate) > 10) {
        fprintf(stderr, "Could not set OSS sample rate to %d (set to %d instead)\n", format->sample_rate, tmp);
        exit(1);
    }
}

/* "Public" function, called from a libspotify callback */
int audio_delivery(const sp_audioformat* format, const void* frames, int num_frames) {
    int ret;

    /* What are we supposed to do here? */
    if (num_frames == 0) {
        /* Pause: close the device */
        oss_close();
        return 0;
    }
    else {
        if (g_oss_fd == -1) {
            /* Some frames to play, but the device is closed: open it and set it up */
            oss_open();
            oss_setup(format);
        }

        /* Is the device ready to be written to? */
        ret = poll(&g_pfd, 1, 0);
        if (ret == -1) {
            perror("Can't poll OSS device");
            exit(1);
        }
        else if (ret == 0) {
            /* Timeout: no data can be written (it would block), tell libspotify about it */
            return 0;
        }

        /* Ok, we can write to the device safely */
        ret = write(g_oss_fd, frames, g_oss_frame_size * num_frames);
        if (ret == -1) {
            perror("Can't write to OSS device");
            exit(1);
        }

        return ret / g_oss_frame_size;
    }
}
