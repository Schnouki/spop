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

#if __APPLE__
// In Mac OS X 10.5 and later trying to use the daemon function gives a “‘daemon’ is deprecated”
// error, which prevents compilation because we build with "-Werror".
// Since this is supposed to be portable cross-platform code, we don't care that daemon is
// deprecated on Mac OS X 10.5, so we use this preprocessor trick to eliminate the error message.
// See: <http://www.opensource.apple.com/source/mDNSResponder/mDNSResponder-258.13/mDNSPosix/PosixDaemon.c>
#define daemon fake_daemon_function
#endif

#include <errno.h>
#include <glib.h>
#include <glib-object.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if __APPLE__
#undef daemon
extern int daemon(int, int);
#endif

#include "spop.h"
#include "config.h"
#include "interface.h"
#include "plugin.h"
#include "queue.h"
#include "spotify.h"

static const char* copyright_notice =
    "spop Copyright (C) " SPOP_YEAR " Thomas Jost and the spop contributors\n"
    "This program comes with ABSOLUTELY NO WARRANTY.\n"
    "This is free software, and you are welcome to redistribute it under certain conditions.\n"
    "See the COPYING file bundled with this program for details.\n"
    "Powered by SPOTIFY(R) CORE\n";

/***************************************
 *** Global variables and prototypes ***
 ***************************************/
gboolean debug_mode   = FALSE;
gboolean verbose_mode = FALSE;

static void exit_handler_init();
static void exit_handler();
static void sigint_handler(int signum);

/* Logging stuff */
static GRecMutex g_log_mutex;
static const gchar* g_log_file_path = NULL;
static GIOChannel* g_log_channel = NULL;
static void logging_init();
static void sighup_handler(int signum);
static void spop_log_handler(const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer user_data);


/**********************
 *** Initialization ***
 **********************/
int main(int argc, char** argv) {
    gboolean daemon_mode = TRUE;
    const char* username;
    const char* password;
    GMainLoop* main_loop;

    /* Parse command line options */
    int opt;
    while ((opt = getopt(argc, argv, "dfhvc:")) != -1) {
        switch (opt) {
        case 'd':
            debug_mode = TRUE;
        case 'v':
            verbose_mode = TRUE;
        case 'f':
            daemon_mode = FALSE; break;
	case 'c':
	    g_setenv("SPOPD_CONFIG", optarg, 1); break;
        default:
            printf("Usage: spopd [options]\n"
                   "Options:\n"
                   "  -d        debug mode (implies -f and -v)\n"
                   "  -f        run in foreground (default: fork to background)\n"
                   "  -v        verbose mode (implies -f)\n"
                   "  -c        location and name of configuration file\n"
                   "  -h        display this message\n");
            return 0;
        }
    }

    #if !GLIB_CHECK_VERSION(2, 35, 0)
        g_type_init();
    #endif

    g_set_application_name("spop " SPOP_VERSION);
    g_set_prgname("spop");

    /* PulseAudio properties */
    g_setenv("PULSE_PROP_application.name", "spop " SPOP_VERSION, TRUE);
    g_setenv("PULSE_PROP_media.role", "music", TRUE);
    //g_setenv("PULSE_PROP_application.icon_name", "music", TRUE);

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
        printf("Switching to daemon mode...\n");
        if (daemon(0, 0) != 0)
            g_error("Error while forking process: %s", g_strerror(errno));

    }

    /* Init essential stuff */
    main_loop = g_main_loop_new(NULL, FALSE);
    exit_handler_init();

    /* Read username and password */
    username = config_get_string("spotify_username");
    password = config_get_string("spotify_password");

    /* Init plugins */
    plugins_init();

    /* Init login */
    session_init();
    session_login(username, password);

    /* Init various subsystems */
    interface_init();

    /* Event loop */
    g_main_loop_run(main_loop);

    return 0;
}


/***************************************************
 *** Exit handler (called on normal termination) ***
 ***************************************************/
void exit_handler_init() {
    /* On normal exit, use exit_handler */
    atexit(exit_handler);

    /* Trap SIGINT (Ctrl+C) to also use exit_handler */
    if (signal(SIGINT, sigint_handler) == SIG_ERR)
        g_error("Can't install signal handler: %s", g_strerror(errno));
}

void exit_handler() {
    g_debug("Entering exit handler...");

    plugins_close();
    session_logout();

    g_message("Exiting.");
}

void sigint_handler(int signum) {
    g_info("Got SIGINT.");

    exit_handler();

    /* The proper way of doing this is to kill ourself (not to call exit()) */
    if (signal(SIGINT, SIG_DFL) == SIG_ERR)
        g_error("Can't install signal handler: %s", g_strerror(errno));
    raise(SIGINT);
}


/**************************
 *** Logging management ***
 **************************/
void logging_init() {
    /* Set the default handler */
    g_log_set_default_handler(spop_log_handler, NULL);

    /* Open the log file */
    g_log_file_path = config_get_string_opt("log_file", "");
    if (strlen(g_log_file_path) > 0) {
        /* Install a handler so that we can reopen the file on SIGHUP */
        if (signal(SIGHUP, sighup_handler) == SIG_ERR)
            g_error("Can't install signal handler: %s", g_strerror(errno));

        /* And open the file using this handler :) */
        sighup_handler(0);
    }
}

void sighup_handler(int signum) {
    GError* err = NULL;

    g_rec_mutex_lock(&g_log_mutex);
    if (g_log_channel && (g_io_channel_shutdown(g_log_channel, TRUE, &err) != G_IO_STATUS_NORMAL))
        g_error("Can't close log file: %s", err->message);

    if (strlen(g_log_file_path) > 0) {
        g_log_channel = g_io_channel_new_file(g_log_file_path, "a", &err);
        if (!g_log_channel)
            g_error("Can't open log file (%s): %s", g_log_file_path, err->message);
    }
    g_rec_mutex_unlock(&g_log_mutex);
}

void spop_log_handler(const gchar* log_domain, GLogLevelFlags log_level, const gchar* message, gpointer user_data) {
    GString* log_line = NULL;
    GDateTime* datetime;
    gchar* timestr;

    GError* err = NULL;
    gchar* level = "";

    g_rec_mutex_lock(&g_log_mutex);

    /* Convert log_level to a string */
    if (log_level & G_LOG_LEVEL_ERROR)
        level = "ERR ";
    else if (log_level & G_LOG_LEVEL_CRITICAL)
        level = "CRIT";
    else if (log_level & G_LOG_LEVEL_WARNING)
        level = "WARN";
    else if (log_level & G_LOG_LEVEL_MESSAGE)
        level = "MSG ";
    else if (log_level & G_LOG_LEVEL_INFO) {
        if (!verbose_mode) {
            g_rec_mutex_unlock(&g_log_mutex);
            return;
        }
        level = "INFO";
    }
    else if (log_level & G_LOG_LEVEL_DEBUG) {
        if (!debug_mode) {
            g_rec_mutex_unlock(&g_log_mutex);
            return;
        }
        level = "DBG ";
    }
    else if (log_level & G_LOG_LEVEL_LIBSPOTIFY)
        level = "SPTF";
    else
        g_warn_if_reached();

    /* Allocate memory and read date/time */
    log_line = g_string_sized_new(1024);
    if (!log_line)
        g_error("Can't allocate memory.");

    datetime = g_date_time_new_now_local();
    if (!datetime)
        g_error("Can't get the current date.");
    timestr = g_date_time_format(datetime, "%Y-%m-%d %H:%M:%S");
    if (!timestr)
        g_error("Can't format current date to a string.");

    /* Format the message that will be displayed and logged */
    if (log_domain)
        g_string_printf(log_line, "%s ", log_domain);
    g_string_append_printf(log_line, "%s [%s] %s\n", timestr, level, message);

    /* Free memory used by datetime and timestr */
    g_date_time_unref(datetime);
    g_free(timestr);

    /* First display to stderr... */
    fprintf(stderr, "%s", log_line->str);
    /* ... then to the log file. */
    if (g_log_channel) {
        if (g_io_channel_write_chars(g_log_channel, log_line->str, log_line->len, NULL, &err) != G_IO_STATUS_NORMAL)
            g_error("Can't write to log file: %s", err->message);
        if (g_io_channel_flush(g_log_channel, &err) != G_IO_STATUS_NORMAL)
            g_error("Can't flush log file: %s", err->message);
    }

    g_rec_mutex_unlock(&g_log_mutex);
}
