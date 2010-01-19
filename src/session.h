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

#ifndef SESSION_H
#define SESSION_H

#include <spotify/api.h>

/* Functions called directly from spop */
void session_init();
void session_login(const char* username, const char* password);
void session_events_loop();

/* Callbacks */
void cb_logged_in(sp_session* session, sp_error error);
void cb_logged_out(sp_session* session);
void cb_message_to_user(sp_session* session, const char* message);
void cb_notify_main_thread(sp_session* session);
void cb_log_message(sp_session* session, const char* data);

#endif
