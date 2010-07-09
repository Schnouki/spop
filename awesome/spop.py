#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# This file is part of spop.
#
# spop is free software: you can redistribute it and/or modify it under the
# terms of the GNU General Public License as published by the Free Software
# Foundation, either version 3 of the License, or (at your option) any later
# version.
#
# spop is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
# A PARTICULAR PURPOSE. See the GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along with
# spop. If not, see <http://www.gnu.org/licenses/>.

import dbus
import pynotify
import select
import socket
import time

NOTIF_SUMMARY = "spop update"

class SpopListener(object):
    def __init__(self, server, port):
        self.server = server
        self.port = port

        self.awesome_client = None
        self.notif = None

    def connect(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((self.server, self.port))
        sockf = sock.makefile("r")
        sockf.readline()

        return sock

    def notify(self, msg):
        if self.notif is None:
            self.notif = pynotify.Notification(summary=NOTIF_SUMMARY)
            self.notif.set_timeout(8000)
            self.notif.set_urgency(pynotify.URGENCY_LOW)

        # Escape string before sending it to libnotify
        msg = msg.replace("&", "&amp;")

        self.notif.update(summary=NOTIF_SUMMARY, message=msg)
        self.notif.show()

    def awesome(self, msg):
        if self.awesome_client is None:
            bus = dbus.SessionBus()
            awesome_proxy = bus.get_object("org.naquadah.awesome.awful", "/")
            self.awesome_client = awesome_proxy.get_dbus_method("Eval", dbus_interface="org.naquadah.awesome.awful.Remote")

        # Escape string before sending it to Awesome
        msg = msg.replace("\"", "\\\"")
        msg = msg.replace("&", "&amp;")
        self.awesome_client("tb_spop.text = \"%s \"\n" % msg)

    def read_status(self, sock, notif=True):
        album, artist, title, dur, pos = "", "", "", "", ""
        qpos, qtot = 1, 1
        paused, stopped = False, False

        sockf = sock.makefile("r")

        # Read status from network
        while True:
            line = sockf.readline().strip()
            if line.startswith("- ") or line.startswith("+ "):
                break
            
            (name, val) = line.split(": ", 1)
            
            if name == "Status":
                if val == "stopped":
                    stopped = True
                elif val == "paused":
                    paused = True
            elif name == "Total tracks":
                qtot = int(val)
            elif name == "Current track":
                qpos = int(val)
            elif name == "Album":
                album = val
            elif name == "Artist":
                artist = val
            elif name == "Title":
                title = val
            elif name == "Duration":
                dur = val
            elif name == "Position":
                pos = val

        # Prepare data to display
        wtxt = ""
        ntxt = ""
        if stopped:
            wtxt = " [stopped] "
            ntxt = "<b>[stopped]</b>\n%d tracks in queue" % qtot
        else:
            if paused:
                wtxt = " <b>[p]</b>"
                ntxt = "<b>[paused]</b>\n"

            short_title = title
            if len(short_title) >= 30:
                short_title = title[:30] + "…"

            wtxt += " [<b>%s:</b> %s / %s]" % (col("#afd", qpos), col("#adf", artist), col("#fad", short_title))
            wtxt += " [<b>%s</b>/%s]" % (col("#dfa", pos), col("#dfa", dur))

            if notif:
                ntxt += "\nNow playing track <b>%s</b>/%s:\n\n" % (col("#afd", qpos), col("#afd", qtot))
                ntxt += "\t<b>%s</b>\nby\t<b>%s</b>\n" % (col("#fad", title), col("#adf", artist))
                ntxt += "from\t%s" % col("#fda", album)

        # Send to Awesome
        self.awesome(wtxt)

        # Send as a desktop notification
        if notif:
            self.notify(ntxt)

    def run(self):
        while True:
            try:
                idle_sock = self.connect()
                active_sock = self.connect()
                print "Connected."

                idle_sock.send("idle\n")
                active_sock.send("status\n")
                self.read_status(active_sock)

                while True:
                    r, w, x = select.select([idle_sock], [], [], 1)
                    if len(r) > 0:
                        # Result of an "idle "command"
                        self.read_status(r[0])
                        r[0].send("idle\n")
                    else:
                        # Query
                        active_sock.send("status\n")
                        self.read_status(active_sock, False)
            except Exception, e:
                print(e)
                print("Trying again in 5 seconds...")
                idle_sock.close()
                active_sock.close()
                self.notif = None
                self.awesome_client = None
                time.sleep(5)


def col(c, s):
    return '<span foreground="%s">%s</span>' % (c, str(s))

if __name__ == "__main__":
    pynotify.init(app_name="spop")
    
    sl = SpopListener("localhost", 6602)
    sl.run()
