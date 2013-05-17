-- Copyright (C) 2010, 2011, 2012, 2013 Thomas Jost
--
-- This file is part of spop.
--
-- spop is free software: you can redistribute it and/or modify it under the
-- terms of the GNU General Public License as published by the Free Software
-- Foundation, either version 3 of the License, or (at your option) any later
-- version.
--
-- spop is distributed in the hope that it will be useful, but WITHOUT ANY
-- WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
-- A PARTICULAR PURPOSE. See the GNU General Public License for more details.
--
-- You should have received a copy of the GNU General Public License along with
-- spop. If not, see <http://www.gnu.org/licenses/>.
--
-- Additional permission under GNU GPL version 3 section 7
--
-- If you modify this Program, or any covered work, by linking or combining it
-- with libspotify (or a modified version of that library), containing parts
-- covered by the terms of the Libspotify Terms of Use, the licensors of this
-- Program grant you additional permission to convey the resulting work.

local socket = require("socket")

module("spop")

local server, port
local cycle_status = 0

function init(serv, por)
   server = serv
   port = por
end

function command(s)
   local conn = socket.connect(server, port)
   if conn then
      conn:send(s .. "\nbye\n")
      conn:close()
   end
end

function next()   command("next")   end
function prev()   command("prev")   end
function toggle() command("toggle") end
function stop()   command("stop")   end

function cycle()
   if (cycle_status % 2) == 0 then
      command("repeat")
   else
      command("shuffle")
   end
   cycle_status = cycle_status + 1
end
