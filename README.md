# Welcome to spop!

spop is a Spotify client that works as a daemon (similar to the famous [MPD][]).
It is designed to be as simple and straightforward as possible: run it, control
it with your keyboard and a few scripts, and just forget about it.

## Features
- **Works as a daemon:** no GUI, just start it, it will run in the background and do
  what you want it to do. Your music won't stop playing if your X server crashes!
- **Uses libspotify:** stable, reliable. Not free (a Spotify [premium account][]
  is required), but quite cheap.
- **Written in plain C:** as lightweight as possible, only 300 kB when compiled
  *with debugging symbols*...
- **Few dependencies:** only requires [libspotify][], [Glib][], [JSON-GLib][]
  and [libao][] (or [libsox][]; not required for OSS audio output).
- **Powerful audio effects**: when using [libsox][], you can apply various
  effects to the audio output: equalisation, normalisation, reverb, "karaoke",
  etc. SoX is the [Swiss Army knife of sound processing][sak]!
- **Powerful plugin system:** you can write your own plugin in a few dozens
  lines of code.
- **Simple protocol:** open a TCP connection to the daemon, write a simple
  plain-text command, get an easily parsable JSON output.
- **Portable:** designed to be platform-agnostic, it should work on any platform
  supported both by Glib and libspotify. (But so far it has only been tested on
  Linux and Apple OS X)
- **Free software:** the source code is available under the terms of the GNU
  GPLv3 license (or, at your option, any later version), with an exception
  allowing to distribute code linked against libspotify. Everyone is welcome to
  contribute!

## Plugins
Right now, several plugins are available:

- *notify:* use [libnotify][] for desktop notifications
- *savestate:* keep the current state (queue, current track, etc.) when stopping
  and restarting spop
- *scrobble:* scrobble your music to [LastFM][] or [LibreFM][] (requires [libsoup][])
- *awesome:* keep an eye on your player in [Awesome][], an extremely powerful
  window manager
- *mpris2:* support the [MPRIS2][] standard to control spopd like any other
  media player from your desktop or using the multimedia keys on your keyboard
  (work in progress)

## How to use
1. Install [libspotify][] (preferably using your favorite package manager)
2. Download spop's source code:

        git clone git://github.com/Schnouki/spop.git

3. Prepare your configuration file:

        mkdir -p ~/.config/spop
        cp spop/spopd.conf.sample ~/.config/spop/spopd.conf
        nano ~/.config/spop/spopd.conf

3. Compile and run spop:

        cd spop
        ./build_and_run -fv

4. Connect to the daemon and issue some commands:

        telnet localhost 6602

If you want to install spop somewhere on your system, do the following steps:

    mkdir build
    cd build
    cmake -DCMAKE_INSTALL_PREFIX=/where/to/install ..
    make
    sudo make install

## Commands
At the moment, spop can not modify your playlists or do any search on Spotify
(but this will come...). So you will have to create some playlists using the
official Spotify client. Then, you will be able to use the following commands:

- `ls`: list all your playlists
- `ls pl`: list the contents of playlist number `pl`

---

- `qls`: list the contents of the queue
- `qclear`: clear the contents of the queue
- `qrm tr`: remove track number `tr` from the queue
- `qrm tr1 tr2`: remove tracks `tr1` to `tr2` from the queue

---

- `add pl`: add playlist number `pl` to the queue
- `add pl tr`: add track number `tr` from playlist number `pl` to the queue
- `play pl`: replace the contents of the queue with playlist `pl` and start
  playing
- `play pl tr`: replace the contents of the queue with track `tr` from playlist
  `pl` and start playing

---

- `uinfo uri`: display information about the given Spotify URI
- `uadd uri`: add the given Spotify URI to the queue (playlist, track or album
  only)
- `uplay uri`: replace the contents of the queue with the given Spotify URI
  (playlist, track or album only) and start playing

---

- `search query`: perform a search with the given query

---

- `play`: start playing from the queue
- `toggle` or `pause`: toggle pause mode
- `stop`: stop playback
- `seek pos`: go to position `pos` (in seconds) in the current track
- `next`: switch to the next track in the queue
- `prev`: switch to the previous track in the queue
- `goto tr`: switch to track number `tr` in the queue
- `repeat`: toggle repeat mode
- `shuffle`: toggle shuffle mode

---

- `status`: display informations about the queue, the current track, etc.
- `idle`: wait for something to change (pause, switch to other track, new track
  in queue...), then display `status`. Mostly useful in notification scripts.
- `notify`: unlock all the currently idle sessions, just like if something had
  changed.
- `image`: get the cover image for the current track (base64-encoded JPEG image).
- `offline-status`: display informations about the current status of the offline
  cache (number of offline playlists, sync status...).
- `offline-toggle pl`: toggle offline mode for playlist number `pl`.

---

- `bye`: close the connection to the spop daemon
- `quit`: exit spop

## Furthermore...

This doc is probably lacking a gazillion useful informations, so feel free to
ask me if you have any question regarding spop!

- On GitHub: <https://github.com/inbox/new/Schnouki>
- By e-mail: <`my_nickname@my_nickname.net`> (by the way, my nickname
  is "Schnouki")
- On identi.ca: <http://identi.ca/schnouki>
- On Twitter: <http://www.twitter.com/Schnouki>

[Awesome]: http://awesome.naquadah.org/
[Glib]: http://library.gnome.org/devel/glib/
[JSON-GLib]: http://live.gnome.org/JsonGlib
[libspotify]: http://developer.spotify.com/en/libspotify/overview/
[libao]: http://www.xiph.org/ao/
[libsox]: http://sox.sourceforge.net/
[sak]: http://sox.sourceforge.net/Docs/Features
[libnotify]: http://library.gnome.org/devel/libnotify/
[LastFM]: http://www.last.fm/
[LibreFM]: http://libre.fm/
[libsoup]: http://live.gnome.org/LibSoup
[MPD]: http://mpd.wikia.com/
[MPRIS2]: http://specifications.freedesktop.org/mpris-spec/latest/
[premium account]: http://www.spotify.com/uk/get-spotify/overview/
