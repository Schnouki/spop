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

#ifndef SPOP_H
#define SPOP_H

#include <glib.h>

#define SPOP_VERSION "0.0.1"
#define SPOP_YEAR    "2010, 2011, 2012, 2013"

/* Verbosity */
extern gboolean debug_mode;
extern gboolean verbose_mode;

/* Logging */
#define G_LOG_LEVEL_LIBSPOTIFY (1 << (G_LOG_LEVEL_USER_SHIFT))

#define g_info(...) g_log(G_LOG_DOMAIN, G_LOG_LEVEL_INFO, __VA_ARGS__)
#define g_log_libspotify(...) g_log(G_LOG_DOMAIN, G_LOG_LEVEL_LIBSPOTIFY, __VA_ARGS__)

#endif
