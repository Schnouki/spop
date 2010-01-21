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
#include "plugin.h"
#include "session.h"

int main(int argc, char** argv) {
    printf("spop Copyright (C) " SPOP_YEAR " Thomas Jost\n"
           "This program comes with ABSOLUTELY NO WARRANTY.\n"
           "This is free software, and you are welcome to redistribute it under certain conditions.\n"
           "See the COPYING file bundled with this program for details.\n"
           "Uses SPOTIFY(R) CORE\n\n");

    /* Init plugins */
    init_plugins();

    /* Read username and password */
    char username[80];
    char password[80];
    char* pass;

    printf("Username: ");
    fgets(username, 80, stdin);
    username[strlen(username)-1] = 0; /* remove carriage return */

    pass = getpass("Password: "); /* getpass() is bad, but this is temporary anyway */
    strncpy(password, pass, 80);

    /* Init login */
    session_login(username, password);

    pthread_t t;
    pthread_create(&t, NULL, play_sigur_ros, NULL);

    /* Event loop */
    session_events_loop();

    return 0;
}
