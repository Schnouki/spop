/*
 * Copyright (C) 2010, 2011, 2012, 2013, 2014 Thomas Jost
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

#ifndef INTERFACE_H
#define INTERFACE_H

#include <glib.h>

/* Functions called directly from spop */
void interface_init();

/* Internal functions used to manage the network interface */
typedef enum { CR_OK=0, CR_CLOSE, CR_DEFERED, CR_IDLE } command_result;
gboolean interface_event(GIOChannel* source, GIOCondition condition, gpointer data);
gboolean interface_client_event(GIOChannel* source, GIOCondition condition, gpointer data);
command_result interface_handle_command(GIOChannel* chan, gchar* command);
gboolean interface_write(GIOChannel* source, const gchar* str);
void interface_finalize(const gchar* str, GIOChannel* chan);

/* Notify clients (channels or plugins) that are waiting for an update */
void interface_notify();
void interface_notify_chan(gpointer data, gpointer user_data);
void interface_notify_callback(gpointer data, gpointer user_data);

typedef void (*spop_notify_callback_ptr)(const GString*, gpointer);
gboolean interface_notify_add_callback(spop_notify_callback_ptr func, gpointer data);
gboolean interface_notify_remove_callback(spop_notify_callback_ptr func, gpointer data);

#endif
