/*
 * Copyright (C) 2010, 2011, 2012, 2013, 2014, 2015 The spop contributors
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

#include <arpa/inet.h>
#include <errno.h>
#include <glib.h>
#include <json-glib/json-glib.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "spop.h"
#include "commands.h"
#include "config.h"
#include "config.h"
#include "interface.h"

#include "sd-daemon.h"

const char proto_greetings[] = "spop " SPOP_VERSION "\n";

/* Channels and plugins that have to be notified when something changes
   ("idle" command) */
static GList* g_idle_channels = NULL;
static GList* g_notification_callbacks = NULL;
typedef struct {
    spop_notify_callback_ptr func;
    gpointer data;
} notification_callback;

command_full_descriptor g_commands[] = {
    { "help",    CT_FUNC, { help, {CA_NONE}}, "list all available commands"},

    { "ls",      CT_FUNC, { list_playlists, {CA_NONE}}, "list all your playlists"},
    { "ls",      CT_FUNC, { list_tracks,    {CA_INT, CA_NONE}}, "list the contents of playlist number arg1"},

    { "status",  CT_FUNC, { status,  {CA_NONE}}, "display informations about the queue, the current track, etc."},
    { "notify",  CT_FUNC, { notify,  {CA_NONE}}, "unlock all the currently idle sessions, just like if something had changed"},
    { "repeat",  CT_FUNC, { repeat,  {CA_NONE}}, "toggle repeat mode"},
    { "shuffle", CT_FUNC, { shuffle, {CA_NONE}}, "toggle shuffle mode"},

    { "qls",     CT_FUNC, { list_queue,         {CA_NONE}}, "list the contents of the queue"},
    { "qclear",  CT_FUNC, { clear_queue,        {CA_NONE}}, "clear the contents of the queue"},
    { "qrm",     CT_FUNC, { remove_queue_item,  {CA_INT, CA_NONE}}, "remove track number arg1 from the queue"},
    { "qrm",     CT_FUNC, { remove_queue_items, {CA_INT, CA_INT}}, "remove tracks arg1 to arg2 from the queue"},

    { "play",    CT_FUNC, { play_playlist, {CA_INT, CA_NONE}}, "replace the contents of the queue with playlist arg1 and start playing"},
    { "play",    CT_FUNC, { play_track,    {CA_INT, CA_INT}}, "replace the contents of the queue with track arg1 from playlist arg2 and start playing" },

    { "add",     CT_FUNC, { add_playlist, {CA_INT, CA_NONE}}, "add playlist number arg1 to the queue"},
    { "add",     CT_FUNC, { add_track,    {CA_INT, CA_INT}}, "add track number arg1 from playlist number arg2 to the queue" },

    { "play",    CT_FUNC, { play,   {CA_NONE}}, "start playing from the queue"},
    { "toggle",  CT_FUNC, { toggle, {CA_NONE}}, "toggle pause mode"},
    { "stop",    CT_FUNC, { stop,   {CA_NONE}}, "stop playback"},
    { "seek",    CT_FUNC, { seek,   {CA_INT, CA_NONE}}, "go to position arg1 (in milliseconds) in the current track"},

    { "next",    CT_FUNC, { goto_next, {CA_NONE}}, "switch to the next track in the queue"},
    { "prev",    CT_FUNC, { goto_prev, {CA_NONE}}, "switch to the previous track in the queue"},
    { "goto",    CT_FUNC, { goto_nb,   {CA_INT, CA_NONE}}, "switch to track number arg1 in the queue"},

    { "offline-status", CT_FUNC, { offline_status, {CA_NONE}}, "display informations about the current status of the offline cache (number of offline playlists, sync status...)"},
    { "offline-toggle", CT_FUNC, { offline_toggle, {CA_INT, CA_NONE}}, "toggle offline mode for playlist number arg1"},

    { "image",   CT_FUNC, { image, {CA_NONE}}, "get the cover image for the current track (base64-encoded JPEG image)"},

    { "uinfo",   CT_FUNC, { uri_info, {CA_URI, CA_NONE}}, "display information about the given Spotify URI arg1"},
    { "uadd",    CT_FUNC, { uri_add,  {CA_URI, CA_NONE}}, "add the given Spotify URI arg1 to the queue (playlist, track or album only)"},
    { "uplay",   CT_FUNC, { uri_play, {CA_URI, CA_NONE}}, "replace the contents of the queue with the given Spotify URI arg1 (playlist, track or album only) and start playing"},
    { "uimage",  CT_FUNC, { uri_image,      {CA_URI, CA_NONE}}, "get the cover image for the given URI"},
    { "uimage",  CT_FUNC, { uri_image_size, {CA_URI, CA_INT}},  "get the cover image for a given URI; size must be 0 (normal), 1 (large) or 2 (small)"},

    { "star",    CT_FUNC, { toggle_star, {CA_NONE}},        "toggle the \"starred\" status of the current track"},
    { "ustar",   CT_FUNC, { uri_star,    {CA_URI, CA_INT}}, "set the \"starred\" status of the given Spotify URI arg1 (playlist, track or album) to arg2 (0 or 1)"},

    { "search",  CT_FUNC, { search, {CA_STR, CA_NONE}}, "perform a search with the given query arg1"},

    { "bye",     CT_BYE,  {}, "close the connection to the spop daemon"},
    { "quit",    CT_QUIT, {}, "exit spop"},
    { "idle",    CT_IDLE, {}, "wait for something to change (pause, switch to other track, new track in queue...), then display status. Mostly useful in notification scripts"},

    {  NULL, 0, {}}
};

/* Internal helper */
static void interface_init_chan(int sock) {
    /* Create an IO channel and add it to the main loop */
    GIOChannel* chan = g_io_channel_unix_new(sock);
    if (!chan)
        g_error("Can't create IO channel for the main socket.");
    g_io_channel_set_close_on_unref(chan, TRUE);
    g_io_add_watch(chan, G_IO_IN|G_IO_HUP, interface_event, NULL);
}

/* Functions called directly from spop */
void interface_init() {
    /* Try to use systemd socket activation */
    int n, sock;

    n = sd_listen_fds(1);
    if (n < 0)
        g_error("Can't check file descriptors passed by the system manager: %s", g_strerror(errno));
    else if (n > 0) {
        /* Use these sockets */
        for (sock = SD_LISTEN_FDS_START; sock < SD_LISTEN_FDS_START + n; sock++) {
            interface_init_chan(sock);
        }
        g_info("Listening to %d systemd sockets", n);
    }
    else {
        /* Traditional socket creation... */
        const char* ip_addr;
        const char* port;
        struct addrinfo hints;
        struct addrinfo* res;
        struct addrinfo* rp;
        int _true = 1;
        int ret;

        /* Get what we need from the config */
        ip_addr = config_get_string_opt("listen_address", NULL);
        port = config_get_string_opt("listen_port", "6602");

        /* Get corresponding addrinfo's */
        bzero(&hints, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG | AI_NUMERICHOST | AI_NUMERICSERV;
        ret = getaddrinfo(ip_addr, port, &hints, &res);
        if (ret != 0)
            g_error("Can't get address info: %s", gai_strerror(ret));

        /* Handle each address */
        for (rp = res; rp != NULL; rp = rp->ai_next) {
            char hostname[NI_MAXHOST];

            ret = getnameinfo(rp->ai_addr, rp->ai_addrlen, hostname, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
            if (ret != 0)
                g_error("Can't convert address to text: %s", gai_strerror(ret));
            g_debug("Will listen on %s:%s...", hostname, port);

            /* Create the socket */
            sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (sock < 0)
                g_error("Can't create socket: %s", g_strerror(errno));
            if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &_true, sizeof(int)) == -1)
                g_error("Can't set socket options: %s", g_strerror(errno));

            /* Bind the socket */
            if (bind(sock, rp->ai_addr, rp->ai_addrlen) != 0)
                g_error("Can't bind socket: %s", g_strerror(errno));

            /* Start listening */
            if (listen(sock, SOMAXCONN) != 0)
                g_error("Can't listen on socket: %s", g_strerror(errno));

            interface_init_chan(sock);
            g_info("Listening on %s: %s", hostname, port);
        }

        freeaddrinfo(res);
    }
}

/* Interface event -- accept connections, create IO channels for clients */
gboolean interface_event(GIOChannel* source, GIOCondition condition, gpointer data) {
    int sock;
    struct sockaddr client_addr;
    socklen_t addrlen;
    int client;
    GIOChannel* client_chan;
    char client_hostname[NI_MAXHOST];
    char client_port[NI_MAXSERV];

    sock = g_io_channel_unix_get_fd(source);

    /* Accept the connection */
    addrlen = sizeof(client_addr);
    client = accept(sock, &client_addr, &addrlen);
    if (client == -1)
        g_error("Can't accept connection");

    /* Get client IP and port */
    int ret = getnameinfo(&client_addr, addrlen, client_hostname, NI_MAXHOST, client_port, NI_MAXSERV,
                          NI_NUMERICHOST | NI_NUMERICSERV);
    if (ret != 0)
        g_error("Can't convert address to text: %s", gai_strerror(ret));

    g_info("[ie:%d] Connection from %s:%s", client, client_hostname, client_port);

    /* Create IO channel for the client, send greetings, and add it to the main loop */
    client_chan = g_io_channel_unix_new(client);
    if (!client_chan)
        g_error("[ie:%d] Can't create IO channel for the client socket.", client);
    g_io_channel_set_close_on_unref(client_chan, TRUE);

    if (!interface_write(client_chan, proto_greetings))
        goto ie_client_clean;

    g_io_add_watch(client_chan, G_IO_IN|G_IO_HUP, interface_client_event, NULL);

    return TRUE;

 ie_client_clean:
    g_io_channel_shutdown(client_chan, TRUE, NULL);
    g_io_channel_unref(client_chan);
    g_debug("[ie:%d] Connection closed.", client);

    return TRUE;
}

/* Handle communications with the client. */
gboolean interface_client_event(GIOChannel* source, GIOCondition condition, gpointer data) {
    GString* buffer = NULL;
    GError* err = NULL;
    GIOStatus status;
    int client;
    command_result cr = CR_OK;

    client = g_io_channel_unix_get_fd(source);

    /* Ready for reading? */
    if (condition & G_IO_IN) {
        buffer = g_string_sized_new(1024);
        if (!buffer) {
            g_warning("[ice:%d] Can't allocate buffer.", client);
            goto ice_client_clean;
        }

        /* Read exactly one command  */
        status = g_io_channel_read_line_string(source, buffer, NULL, &err);
        if (status == G_IO_STATUS_EOF) {
            g_debug("[ice:%d] Connection reset by peer.", client);
            goto ice_client_clean;
        }
        else if (status != G_IO_STATUS_NORMAL) {
            g_debug("[ice:%d] Can't read from IO channel: %s", client, err->message);
            goto ice_client_clean;
        }

        buffer->str[buffer->len-1] = '\0';
        g_debug("[ice:%d] Received command: %s", client, buffer->str);
        buffer->str[buffer->len-1] = '\n';

        /* Parse and run the command */
        cr = interface_handle_command(source, buffer->str);
        g_string_free(buffer, TRUE);
        buffer = NULL;

        /* "idle" command? */
        if (cr == CR_IDLE) {
            /* Add to list of idle channels */
            g_idle_channels = g_list_prepend(g_idle_channels, source);
        }
    }

    /* Received hangup? */
    if (condition & G_IO_HUP) {
        g_debug("[ice:%d] Connection hung up", client);
        goto ice_client_clean;
    }

    if (cr != CR_CLOSE)
        return TRUE;

 ice_client_clean:
    if (buffer)
        g_string_free(buffer, TRUE);
    g_idle_channels = g_list_remove(g_idle_channels, source);
    g_io_channel_shutdown(source, TRUE, NULL);
    g_io_channel_unref(source);
    g_info("[ice:%d] Connection closed.", client);

    return FALSE;
}

/* Parse the command and execute it */
command_result interface_handle_command(GIOChannel* chan, gchar* command){
    GError* err = NULL;
    gint argc;
    gchar** argv_;
    gchar** argv;
    size_t i;

    /* Parse the command in a shell-like fashion */
    if (!g_shell_parse_argv(g_strstrip(command), &argc, &argv_, &err)) {
        g_debug("Command parser error: %s", err->message);
        interface_write(chan, "{ \"error\": \"invalid command\" }\n");
        return CR_OK;
    }

    /* Copy argv_ to the stack so it's easier to use... */
    argv = g_newa(gchar*, argc);
    for(i=0; i < argc; i++) {
        argv[i] = g_newa(gchar, strlen(argv_[i]+1));
        strcpy(argv[i], argv_[i]);
    }
    g_strfreev(argv_);

    g_debug("Command: [%s] with %d parameter(s)", argv[0], argc-1);

    /* Now execute the command */
    command_full_descriptor* cmd_desc = NULL;

    for (i=0; g_commands[i].name != NULL; i++) {
        int nb_args = 0;
        while ((nb_args < MAX_CMD_ARGS) && (g_commands[i].desc.args[nb_args] != CA_NONE))
            nb_args += 1;
        if ((strcmp(g_commands[i].name, argv[0]) == 0) && (nb_args == argc-1)) {
            cmd_desc = &(g_commands[i]);
            break;
        }
    }
    if (!cmd_desc) {
        interface_write(chan, "{ \"error\": \"unknown command\" }\n");
        return CR_OK;
    }

    /* Handle "normal" and "special" commands separately. */
    switch (cmd_desc->type) {
    case CT_FUNC: {
        gboolean ret;

        ret = command_run((command_finalize_func) interface_finalize, chan, &(cmd_desc->desc), argc, argv);
        return (ret ? CR_OK : CR_DEFERED);
    }

    case CT_BYE:
        interface_write(chan, "Bye bye!\n");
        return CR_CLOSE;

    case CT_QUIT:
        g_message("Got a quit command, exiting...");
        exit(0);

    case CT_IDLE:
        return CR_IDLE;
    }

    return CR_OK;
}

gboolean interface_write(GIOChannel* chan, const gchar* str) {
    GIOStatus status;
    GError* err = NULL;
    int client = g_io_channel_unix_get_fd(chan);

    if (str && chan->is_writeable) {
        status = g_io_channel_write_chars(chan, str, -1, NULL, &err);
        if (status != G_IO_STATUS_NORMAL) {
            if (err)
                g_debug("[iw:%d] Can't write to IO channel (%d)", client, status);
            else
                g_debug("[iw:%d] Can't write to IO channel (%d): %s", client, status, err->message);
            return FALSE;
        }
    }

    status = g_io_channel_flush(chan, &err);
    if (status != G_IO_STATUS_NORMAL) {
        if (err)
            g_debug("[iw:%d] Can't flush IO channel (%d)", client, status);
        else
            g_debug("[iw:%d] Can't flush IO channel (%d): %s", client, status, err->message);
        return FALSE;
    }

    return TRUE;
}

void interface_finalize(const gchar* str, GIOChannel* chan) {
    interface_write(chan, str);
}


/* Notify clients (channels or plugins) that are waiting for an update */
/* TODO: use a command_finalize_func for that too */
void interface_notify() {
    GString* str = g_string_sized_new(1024);
    JsonBuilder* jb = json_builder_new();
    command_context ctx = { jb, NULL, NULL };

    json_builder_begin_object(jb);
    status(&ctx);
    json_builder_end_object(jb);

    JsonGenerator *gen = json_generator_new();
    g_object_set(gen, "pretty", config_get_bool_opt("pretty_json", FALSE), NULL);
    json_generator_set_root(gen, json_builder_get_root(jb));

    gchar *tmp = json_generator_to_data(gen, NULL);
    g_string_assign(str, tmp);
    g_string_append(str, "\n");

    g_object_unref(gen);
    g_object_unref(jb);
    g_free(tmp);

    /* First notify idle channels */
    g_list_foreach(g_idle_channels, interface_notify_chan, str);
    g_list_free(g_idle_channels);
    g_idle_channels = NULL;

    /* Then call callbacks from plugins */
    g_list_foreach(g_notification_callbacks, interface_notify_callback, str);

    g_string_free(str, TRUE);
}

void interface_notify_chan(gpointer data, gpointer user_data) {
    GIOChannel* chan = data;
    GString* str = user_data;

    interface_write(chan, str->str);
}

void interface_notify_callback(gpointer data, gpointer user_data) {
    notification_callback* ncb = (notification_callback*) data;
    const GString* status = (const GString*) user_data;

    ncb->func(status, ncb->data);
}

gboolean interface_notify_add_callback(spop_notify_callback_ptr func, gpointer data) {
    notification_callback* ncb;
    GList* cur;

    /* Is there already such a callback/data couple in the list? */
    cur = g_notification_callbacks;
    while (cur != NULL) {
        ncb = cur->data;
        if ((ncb->func == func) && (ncb->data == data))
            return FALSE;
        cur = cur->next;
    }

    /* Callback/data not in the list: add them */
    ncb = g_malloc(sizeof(notification_callback));
    ncb->func = func;
    ncb->data = data;
    g_notification_callbacks = g_list_prepend(g_notification_callbacks, ncb);
    return TRUE;
}
