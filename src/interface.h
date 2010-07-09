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

#ifndef INTERFACE_H
#define INTERFACE_H

#include <glib.h>

/* Functions called directly from spop */
void interface_init();

/* Internal functions used to manage the network interface */
gboolean interface_event(GIOChannel* source, GIOCondition condition, gpointer data);
gboolean interface_client_event(GIOChannel* source, GIOCondition condition, gpointer data);
gboolean interface_handle_command(gchar** command, GString* result);

#endif
