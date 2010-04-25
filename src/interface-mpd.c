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

static int g_sockfd;
static pthread_t if_t;

const char proto_greetings[] = "OK MPD " SPOP_VERSION "-spop\n";
const char proto_ok[] = "OK\n";
const char proto_list_ok[] = "list_OK\n";
const char proto_ack[] = "ACK\n";
const int proto_greetings_len = sizeof(proto_greetings);
const int proto_ok_len = sizeof(proto_ok);
const int proto_list_ok_len = sizeof(proto_list_ok);
const int proto_ack_len = sizeof(proto_ack);


/* Prototypes of the internal functions defined here */
void* interface_thread(void* data);
int interface_parse_buffer(int sock, const char* buffer, size_t len);
void interface_parse_command(const char* cmd, size_t len, int* argc, char*** argv);

/* Functions called directly from spop */
void interface_init() {
    const char* ip_addr;
    int port;
    int true = 1;
    struct sockaddr_in addr;

    if (config_get_string_opt("listen_address", &ip_addr) != CONFIG_FOUND)
        ip_addr = "127.0.0.1";
    if (config_get_int_opt("listen_port", &port) != CONFIG_FOUND)
        port = 6600;

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

/* Interface thread -- accepts connections, reads commands, execute them */
void* interface_thread(void* data) {
    struct sockaddr_in client_addr;
    socklen_t sin_size;
    char buffer[8192];
    int client;
    int sent, recvd;

    while (1) {
        sin_size = sizeof(struct sockaddr_in);
        client = accept(g_sockfd, (struct sockaddr*) &client_addr, &sin_size);
        if (client == -1) {
            fprintf(stderr, "Can't accept connection");
            exit(1);
        }

        fprintf(stderr, "Connection from (%s, %d)\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        /* Greetings to the client */
        sent = send(client, proto_greetings, proto_greetings_len, 0);
        if (sent == -1) {
            perror("Could not send data");
            exit(1);
        }

        /* Now read commands */
        while (1) {
            bzero(buffer, 8192*sizeof(char));
            recvd = recv(client, buffer, 8191, 0);
            if (recvd == -1) {
                perror("Can't recv data");
                break;
            }
            else if (recvd == 0) {
                fprintf(stderr, "Connection closed\n");
                close(client);
                break;
            }
            buffer[recvd] = 0;

            if (g_debug)
                fprintf(stderr, "Received %d bytes: %s\n", recvd, buffer);
            if (interface_parse_buffer(client, buffer, recvd)) {
                /* Error while executing commands */
                send(client, proto_ack, proto_ack_len, 0);
                fprintf(stderr, "Connection closed (invalid command)\n");
                close(client);
                break;
            }
        }
    }

    return NULL;
}


/* Parse a buffer, extract a command list, and execute them */
int interface_parse_buffer(int sock, const char* buffer, size_t len) {
    int cmd_count = 0;
    int i, j;
    int begin;

    int* argcs = NULL;
    char*** argvs = NULL;

    /* How many commands are there? */
    for (i=0; i < len; i++)
        if (buffer[i] == '\n') cmd_count += 1;
    if (g_debug)
        fprintf(stderr, "%d commands\n", cmd_count);
    if (cmd_count == 0) {
        return 1;
    }

    /* Allocate memory to store commands and their arguments */
    argcs = malloc(cmd_count * sizeof(int));
    if (argcs == NULL) {
        perror("Can't allocate memory");
        exit(1);
    }
    argvs = malloc(cmd_count * sizeof(char**));
    if (argvs == NULL) {
        perror("Can't allocate memory");
        exit(1);
    }

    /* Now parse each command */
    begin = 0;
    j = 0;
    for (i=1; i<len; i++) {
        if (buffer[i] == '\n') {
            /* Include the trailing \n in the string */
            interface_parse_command(&buffer[begin], i - begin + 1, &argcs[j], &argvs[j]);
            j += 1;
            begin = i+1;
        }
    }

    /* Run each command */
    for (i=0; i < cmd_count; i++) {
        /* TODO */
        /* For now, only print the command :) */
        printf("Command %d is [%s], called with %d arguments\n", i, argvs[i][0], argcs[i]);
        for (j=1; j < argcs[i]; j++)
            printf("  arg %d: [%s]\n", j, argvs[i][j]);
    }

    /* Free memory */
    for (i=0; i < cmd_count; i++) {
        for (j=0; j < argcs[i]; j++)
            free(argvs[i][j]);
        free(argvs[i]);
    }
    free(argcs);
    free(argvs);

    return 0;
}

/* Parse one command and extract its arguments */
void interface_parse_command(const char* cmd, size_t len, int* argc, char*** argv) {
    int subs_count = 0;
    int i, j;
    int inside_quotes, inside_word;
    int idx_boss, eoss;

    /* How many substrings are there?
     * 
     * To simplify, we assume there is no \t in the cmd and. If there is, it will be
     * treated as a regular character. Empty strings ("") are treated as valid
     * arguments. 
     *
     * Test case: [ something arg   "arg in quotes" strange"thing "" other   ]
     * should be 6 strings... */
    inside_quotes = 0;
    inside_word = 0;
    for (i=0; i<len; i++) {
        switch (cmd[i]) {
        case ' ':
        case '\n':
        case '\r':
            /* Count as a new sub-string if we are not inside quotes AND if we were inside a word */
            if (!inside_quotes && inside_word) {
                inside_word = 0;
                subs_count += 1;
            }        
            break;
        case '"':
            if (inside_quotes) {
                /* Closing quote --> new sub-string! */
                inside_quotes = 0;
                inside_word = 0;
                subs_count += 1;
            }
            else {
                /* Opening quote --> don't mark as inside quotes if we are in the middle of a word */
                if (!inside_word)
                    inside_quotes = 1;
            }
            break;
        default:
            /* Any other character --> part of a word */
            inside_word = 1;
        }
        if (g_debug)
            fprintf(stderr, "  [%c] iw: %d iq: %d sc: %d\n", cmd[i], inside_word, inside_quotes, subs_count);
    }
    if (g_debug)
        fprintf(stderr, "%d substrings\n", subs_count);
    
    /* Allocate memory */
    *argc = subs_count;
    *argv = malloc(subs_count*sizeof(char*));
    if (*argv == NULL) {
        perror("Can't allocate memory for command arguments");
        exit(1);
    }

    /* Now extract substrings */
    inside_quotes = 0;
    inside_word = 0;
    j = 0;
    idx_boss = -1;
    eoss = 0;
    for (i=0; i<len; i++) {
        switch (cmd[i]) {
        case ' ':
        case '\n':
        case '\r':
            /* End of sub-string if we are not inside quotes AND if we were inside a word */
            if (!inside_quotes && inside_word) {
                inside_word = 0;
                eoss = 1;
            }
            break;
        case '"':
            if (inside_quotes) {
                /* Closing quote --> end of a sub-string */
                inside_quotes = 0;
                inside_word = 0;
                eoss = 1;
            }
            else {
                /* Opening quote --> begin of sub-string unless we are already inside a word */
                if (!inside_word) {
                    inside_quotes = 1;
                    idx_boss = i+1;
                }
            }
            break;
        default:
            /* Any other character --> maybe the beginning of a sub-string */
            inside_word = 1;
            if (idx_boss == -1)
                idx_boss = i;
        }
        if (g_debug)
            fprintf(stderr, "  [%c] iw: %d iq: %d sc: %d boss: %d eoss: %d\n", cmd[i], inside_word, inside_quotes, subs_count, idx_boss, eoss);

        /* Did we reach the end of a sub-string? */
        if (eoss) {
            /* Yes: copy the substring to the argv table */
            (*argv)[j] = strndup(&(cmd[idx_boss]), i - idx_boss);
            if ((*argv)[j] == NULL) {
                perror("Can't allocate memory for a command argument");
                exit(1);
            }
            idx_boss = -1;
            eoss = 0;
            j += 1;
        }
    }
}
