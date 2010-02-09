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

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <pthread.h>

#include "spop.h"
#include "config.h"
#include "plugin.h"
#include "session.h"

static const char* copyright_notice = 
    "spop Copyright (C) " SPOP_YEAR " Thomas Jost\n"
    "This program comes with ABSOLUTELY NO WARRANTY.\n"
    "This is free software, and you are welcome to redistribute it under certain conditions.\n"
    "See the COPYING file bundled with this program for details.\n"
    "Powered by "
#ifdef OPENSPOTIFY
    "libopenspotify"
#else
    "SPOTIFY(R) CORE"
#endif
    "\n\n";

static int run_as_daemon = 1;

int real_main() {
    const char* username;
    const char* password;

    /* Init plugins */
    init_plugins();

    /* Read username and password */
    username = config_get_string("spotify_username");
    password = config_get_string("spotify_password");

    /* Init login */
    session_login(username, password);

    pthread_t t;
    pthread_create(&t, NULL, play_sigur_ros, NULL);

    /* Event loop */
    session_events_loop();

    return 0;
}

int main(int argc, char** argv) {
    /* Parse command line options */
    int opt;
    while ((opt = getopt(argc, argv, "fh")) != -1) {
        switch (opt) {
        case 'f':
            run_as_daemon = 0; break;
        default:
            printf("Usage: spopd [options\n"
                   "Options:\n"
                   "  -f        run in foreground (default: fork to background)\n"
                   "  -h        display this message\n");
            return 0;
        }
    }

    if (!run_as_daemon) {
        /* Stay in foreground: do everything here */
        printf(copyright_notice);
    }
    else {
        /* Run in daemon mode: fork to background */
        pid_t pid = fork();
        if (pid < 0) {
            fprintf(stderr, "Error while forking process\n");
            return 1;
        }
        else if (pid > 0) {
            /* Parent process */
            printf("spopd forked to background with pid %d\n", pid);
            return 0;
        }
        /* The child process will continue and run the real_main() function */
    }

    return real_main();    
}
