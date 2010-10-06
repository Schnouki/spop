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

#include <errno.h>
#include <glib.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "spop.h"
#include "config.h"
#include "interface.h"
#include "plugin.h"
#include "queue.h"
#include "spotify.h"

static const char* copyright_notice = 
    "spop Copyright (C) " SPOP_YEAR " Thomas Jost\n"
    "This program comes with ABSOLUTELY NO WARRANTY.\n"
    "This is free software, and you are welcome to redistribute it under certain conditions.\n"
    "See the COPYING file bundled with this program for details.\n"
    "Powered by SPOTIFY(R) CORE\n";

static gboolean daemon_mode  = TRUE;
static gboolean i_am_daemon  = FALSE;
static gboolean debug_mode   = FALSE;
static gboolean verbose_mode = FALSE;

/* Logging stuff */
static const gchar* g_log_file_path = NULL;
static GIOChannel* g_log_channel = NULL;
static void logging_init();
static void sighup_handler(int signum);
static void spop_log_handler(const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer user_data);

int real_main() {
    const char* username;
    const char* password;
    gboolean high_bitrate;
    GMainLoop* main_loop;

    /* Init essential stuff */
    main_loop = g_main_loop_new(NULL, FALSE);

    /* Read username and password */
    username = config_get_string("spotify_username");
    password = config_get_string("spotify_password");

    high_bitrate = config_get_bool_opt("high_bitrate", TRUE);

    /* Init plugins */
    plugins_init();

    /* Init login */
    session_init(high_bitrate);
    session_login(username, password);

    /* Init various subsystems */
    interface_init();

    /* Event loop */
    g_main_loop_run(main_loop);

    return 0;
}

int main(int argc, char** argv) {
    /* Parse command line options */
    int opt;
    while ((opt = getopt(argc, argv, "dfhv")) != -1) {
        switch (opt) {
        case 'd':
            debug_mode = TRUE;
        case 'v':
            verbose_mode = TRUE;
        case 'f':
            daemon_mode = FALSE; break;
        default:
            printf("Usage: spopd [options\n"
                   "Options:\n"
                   "  -d        debug mode (implies -f and -v)\n"
                   "  -f        run in foreground (default: fork to background)\n"
                   "  -v        verbose mode (implies -f)\n"
                   "  -h        display this message\n");
            return 0;
        }
    }

    g_set_application_name("spop " SPOP_VERSION);
    g_set_prgname("spop");
    g_thread_init(NULL);
    
    printf("%s\n", copyright_notice);

    /* Log handler */
    logging_init();

    if (!daemon_mode) {
        /* Stay in foreground: do everything here */
        if (debug_mode)
            g_info("Running in debug mode");
    }
    else {
        /* Run in daemon mode: fork to background */
        pid_t pid = fork();
        if (pid < 0)
            g_error("Error while forking process: %s", g_strerror(errno));
        else if (pid > 0) {
            /* Parent process */
            g_info("Forked to background with pid %d", pid);
            return 0;
        }
        else {
            /* Child process */
            i_am_daemon = TRUE;
        }
        /* The child process will continue and run the real_main() function */
    }

    return real_main();    
}

void logging_init() {
    struct sigaction act;

    /* Set the default handler */
    g_log_set_default_handler(spop_log_handler, NULL);

    /* Open the log file */
    g_log_file_path = config_get_string_opt("log_file", "/var/log/spopd.log");
    if (strlen(g_log_file_path) > 0) {
        /* Install a handler so that we can reopen the file on SIGHUP */
        act.sa_handler = sighup_handler;
        act.sa_flags = 0;
        sigemptyset(&act.sa_mask);
        sigaddset(&act.sa_mask, SIGHUP);
        if (sigaction(SIGHUP, &act, NULL) == -1)
            g_error("Can't install signal handler: %s", g_strerror(errno));

        /* And open the file using this handler :) */
        sighup_handler(0);
    }    
}

void sighup_handler(int signum) {
    GError* err = NULL;

    if (g_log_channel && (g_io_channel_shutdown(g_log_channel, TRUE, &err) != G_IO_STATUS_NORMAL))
        g_error("Can't close log file: %s", err->message);

    if (strlen(g_log_file_path) > 0) {
        g_log_channel = g_io_channel_new_file(g_log_file_path, "a", &err);
        if (!g_log_channel)
            g_error("Can't open log file (%s): %s", g_log_file_path, err->message);
    }
}

void spop_log_handler(const gchar* log_domain, GLogLevelFlags log_level, const gchar* message, gpointer user_data) {
    GString* log_line = NULL;
    GError* err = NULL;
    gchar* level = "";

    if (log_level & G_LOG_LEVEL_ERROR)
        level = "ERR ";
    else if (log_level & G_LOG_LEVEL_CRITICAL)
        level = "CRIT";
    else if (log_level & G_LOG_LEVEL_WARNING)
        level = "WARN";
    else if (log_level & G_LOG_LEVEL_MESSAGE)
        level = "MSG ";
    else if (log_level & G_LOG_LEVEL_INFO) {
        if (!verbose_mode) return;
        level = "INFO";
    }
    else if (log_level & G_LOG_LEVEL_DEBUG) {
        if (!debug_mode) return;
        level = "DBG ";
    }
    else if (log_level & G_LOG_LEVEL_LIBSPOTIFY)
        level = "SPTF";
    else
        g_warn_if_reached();

    log_line = g_string_sized_new(1024);
    if (!log_line)
        g_error("Can't allocate memory.");

    if (log_domain)
        g_string_printf(log_line, "%s ", log_domain);
    g_string_append_printf(log_line, "[%s] %s\n", level, message);
    
    /* First display to stderr... */
    if (!i_am_daemon)
        fprintf(stderr, "%s", log_line->str);
    /* ... then to the log file. */
    if (g_log_channel) {
        if (g_io_channel_write_chars(g_log_channel, log_line->str, log_line->len, NULL, &err) != G_IO_STATUS_NORMAL)
            g_error("Can't write to log file: %s", err->message);
        if (g_io_channel_flush(g_log_channel, &err) != G_IO_STATUS_NORMAL)
            g_error("Can't flush log file: %s", err->message);
    }    
}
