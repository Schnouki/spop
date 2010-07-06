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

local socket = require("socket")

module("spop")

local server, port

function init(serv, por)
   server = serv
   port = por
end

function command(s)
   local conn = socket.connect(server, port)
   if conn then
      conn:send(s .. "\n")
      conn:close()
   end
end

function next()   command("next")   end
function prev()   command("prev")   end
function toggle() command("toggle") end
function stop()   command("stop")   end
