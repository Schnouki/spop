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
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "spop.h"
#include "config.h"
#include "interface.h"
#include "playlist.h"
#include "track.h"

static int g_sockfd;
static pthread_t if_t;

const char proto_greetings[] = "spop " SPOP_VERSION "\n";

/* Prototypes of the internal functions defined here */
void* interface_thread(void* data);
void interface_handle_client(GIOChannel* channel, GString* buffer);
void interface_handle_command(gchar** command, GString* result);

/* Functions called directly from spop */
void interface_init() {
    const char* ip_addr;
    int port;
    int true = 1;
    struct sockaddr_in addr;

    if (config_get_string_opt("listen_address", &ip_addr) != CONFIG_FOUND)
        ip_addr = "127.0.0.1";
    if (config_get_int_opt("listen_port", &port) != CONFIG_FOUND)
        port = 6602;

    /* Create the socket */
    g_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_sockfd < 0) {
        perror("Can't create socket");
        exit(1);
    }
    if (setsockopt(g_sockfd, SOL_SOCKET, SO_REUSEADDR, &true, sizeof(int)) == -1) {
        perror("Can't set socket options");
        exit(1);
    }

    /* Bind the socket */
    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip_addr);
    if (bind(g_sockfd, (struct sockaddr*) &addr, sizeof(struct sockaddr)) == -1) {
        perror("Can't bind socket");
        exit(1);
    }

    /* Start listening */
    if (listen(g_sockfd, 5) == -1) {
        perror("Can't listen on socket");
        exit(1);
    }

    /* Create the interface thread */
    pthread_create(&if_t, NULL, interface_thread, NULL);
    if (g_debug)
        fprintf(stderr, "Listening on %s:%d\n", ip_addr, port);

}

/* Interface thread -- accept connections, read commands, execute them */
void* interface_thread(void* data) {
    struct sockaddr_in client_addr;
    socklen_t sin_size;
    int client;
    GIOChannel* channel;
    GString* buffer;
    GError* chan_err = NULL;

    while (1) {
        sin_size = sizeof(struct sockaddr_in);
        client = accept(g_sockfd, (struct sockaddr*) &client_addr, &sin_size);
        if (client == -1) {
            fprintf(stderr, "Can't accept connection");
            exit(1);
        }

        if (g_debug)
            fprintf(stderr, "Connection from (%s, %d)\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        /* Handle the client */
        channel = g_io_channel_unix_new(client);
        buffer = g_string_sized_new(1024);
        interface_handle_client(channel, buffer);
        g_string_free(buffer, TRUE);
        if (g_io_channel_shutdown(channel, TRUE, &chan_err) != G_IO_STATUS_NORMAL) {
            fprintf(stderr, "Can't shutdown IO channel: %s\n", chan_err->message);
            exit(1);
        }
        g_io_channel_unref(channel);
        close(client);
        if (g_debug)
            fprintf(stderr, "Connection closed.\n");
    }

    return NULL;
}

/* Handle communications with the client */
void interface_handle_client(GIOChannel* channel, GString* buffer) {
    GError* err = NULL;
    GIOStatus status;
    gchar** command;
    GString* result;
   
    /* Send greetings to the client */
    if (g_io_channel_write_chars(channel, proto_greetings, -1, NULL, &err) != G_IO_STATUS_NORMAL) {
        fprintf(stderr, "Can't write to IO channel: %s\n", err->message);
        return;
    }
    if (g_io_channel_flush(channel, &err) != G_IO_STATUS_NORMAL) {
        fprintf(stderr, "Can't flush IO channel: %s\n", err->message);
        return;
    }

    /* Read commands */
    while (1) {
        status = g_io_channel_read_line_string(channel, buffer, NULL, &err);
        if (status == G_IO_STATUS_EOF) {
            if (g_debug) {
                fprintf(stderr, "Connection reset by peer.\n");
                return;
            }
        }
        else if (status != G_IO_STATUS_NORMAL) {
            fprintf(stderr, "Can't read from IO channel: %s\n", err->message);
            return;
        }

        if (g_debug)
            fprintf(stderr, "Received command: %s", buffer->str);

        /* Extract commands */
        command = g_strsplit(g_strstrip(buffer->str), " ", 0);

        /* Parse and run the command, send its result to the IO channel */
        result = g_string_sized_new(1024);
        g_string_assign(result, "");
        interface_handle_command(command, result);
        status = g_io_channel_write_chars(channel, result->str, -1, NULL, &err);

        /* Free allocated memory and deal with errors */
        g_string_free(result, TRUE);
        g_strfreev(command);
        if (status != G_IO_STATUS_NORMAL) {
            fprintf(stderr, "Can't write to IO channel: %s\n", err->message);
            return;
        }
        if (g_io_channel_flush(channel, &err) != G_IO_STATUS_NORMAL) {
            fprintf(stderr, "Can't flush IO channel: %s\n", err->message);
            return;
        }
    }
}

void interface_handle_command(gchar** command, GString* result) {
    int len;
    gchar* cmd;
    gchar* endptr;
    int arg1 = -1;
    int arg2 = -1;

    /* Number of items in the command array */
    len = g_strv_length(command);
    if (len == 0)
        return;

    cmd = command[0];
    if (g_debug) fprintf(stderr, "Command: [%s]", cmd);

    /* Parse the 2 optional numeric arguments */
    if (len >= 2) {
        arg1 = strtol(command[1], &endptr, 0);
        if (endptr == command[1]) {
            arg1 = -1;
            if (g_debug)
                fprintf(stderr, "Invalid argument: %s\n", command[1]);
            g_string_assign(result, "- invalid argument 1\n");
            return;
        }
        if (g_debug) fprintf(stderr, ", arg1: %d", arg1);
    }
    if (len >= 3) {
        arg2 = strtol(command[2], &endptr, 0);
        if (endptr == command[2]) {
            arg1 = -1;
            if (g_debug)
                fprintf(stderr, "Invalid argument: %s\n", command[2]);
            g_string_assign(result, "- invalid argument 2\n");
            return;
        }
        if (g_debug) fprintf(stderr, ", arg2: %d", arg2);
    }
    if (g_debug) fprintf(stderr, "\n");


    /* Now parse the command... */
    if (strcmp(cmd, "ls") == 0) {
        if (arg1 == -1)
            list_playlists(result);
        else
            list_tracks(arg1, result);
    }
    else if (strcmp(cmd, "quit") == 0) {
        exit(0);
    }
    else {
        g_string_assign(result, "- unknown command\n");
        return;
    }

    if (result->len == 0)
        g_string_append(result, "- ERR\n");
    else
        g_string_append(result, "+ OK\n");
}
