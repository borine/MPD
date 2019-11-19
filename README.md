# Music Player Daemon

This fork contains my own experimental code, implementing features that I 
require in my home environment.

At present there are two new features implemented, each maintained on its own
branch. The master branch merges all the features into a single codebase.

The branch __*bluealsa*__ implements new input, output and mixer plugins that
use the D-Bus API of [bluealsa](https://github.com/Arkq/bluez-alsa) to provide
output to, and input from, bluetooth A2DP devices on linux.

The branch __*mythtv*__ allows the use of mpd as a playback device for OTA 
recordings made with [mythtv](https://www.mythtv.org) (soundtrack only, e.g. for 
radio programmes or music concerts etc). This branch is not recommended for 
general use for two reasons:
1. It interfaces directly with the mythtv mysql database. This is deprecated
and obviously liable to fail every time the database schema is updated. I did 
this simply because the supported mythtv "service" API does not provide the
filtering that I needed.
2. It is coded to work with "Freeview" DTV broadcasts in the UK. The hard-coded
SQL queries may not work correctly for other sources.

## Using this fork of MPD with [bluealsa](https://github.com/Arkq/bluez-alsa)

Because this implementation uses bluealsa's __*D-Bus interface*__, *not its ALSA
interface*, it does not suffer from the issues and limitations imposed by 
libasound.

### Build dependencies

The bluealsa plugins require the libdbus-1 development library. On
Debian / Ubuntu this package suffices:
```
libdbus-1-dev
```

Enable bluealsa in the build by adding
`-Dbluealsa=enabled`
to the meson command line

### Runtime configuration

Example mpd.conf:

	# choose any connected bluetooth sink (most useful if only one at a time is connected). 
    audio_output {
        name          "Bluetooth Speaker"
        type          "bluealsa"
        mixer_type    "software"          # optional; default is "hardware"
    }

	# use a specific bluetooth sink
    audio_ouput {
        name          "Bluetooth Headphones"
        type          "bluealsa"
        device        "XX:XX:XX:XX:XX:XX"
        mixer_type    "none"
    }

	# an input block is not necessary unless you wish to override the defaults
    input {
        plugin              "bluealsa"
        default_address     "XX:XX:XX:XX:XX:XX" # optional; default is ""
        default_dbus_suffix "mysuffix"          # optional; default is ""
    }

For inputs, the uri is of the form:

	bluealsa://[BT_ADDRESS][/[DBUS_SUFFIX]

Example input uris:

	bluealsa://
	bluealsa://89:34:FA:00:3D:12
	bluealsa://89:34:FA:00:3D:12/mydbussuffix
	bluealsa:///mydbussuffix

## Using this fork of MPD with [mythtv](https://www.mythtv.org)

### Build dependencies

The mythtv plugins require mysql/mariadb, mysql++, and ffmpeg libraries. On
Debian / Ubuntu these packages suffice:
```
libmysqlclient-dev
libmysql++-dev
libavcodec-dev
libavformat-dev
```
Enable mythtv in the build by adding
`-Dmythtv=true`
to the meson command line

### Runtime configuration

It is necessary to share the mythv recordings directory with nfs or smb if 
the mpd host is not on the same server as the mythtv backend.

It is also necessary to add a configuration block to the mpd.conf file.

    input {
        plugin         "mythtv"
        host           "mythtv.server.address"
        recordings_url "nfs://mythtv.server.address/var/lib/mythtv/recordings"
        database       "mythconverg"
        user           "mythtv"
        password       "mythtv_password"
    }

With these changes it is then possible to get a listing of all mythtv 
recordings from mpd with:

    listplaylistinfo mythtv://

You can limit the list to only radio recordings (actually programmes with 
channel number >= 700 and < 800, which works with UK Freeview):

    listplaylistinfo mythtv://radio

And you can add a recording to the playqueue using the URI reported by the
above commands, for example:

    add mythtv://1707_20181009235800.ts

    
## Upstream original README:

# Music Player Daemon

http://www.musicpd.org

A daemon for playing music of various formats.  Music is played through the 
server's audio device.  The daemon stores info about all available music, 
and this info can be easily searched and retrieved.  Player control, info
retrieval, and playlist management can all be managed remotely.

For basic installation instructions
[read the manual](https://www.musicpd.org/doc/user/install.html).

# Users

- [Manual](http://www.musicpd.org/doc/user/)
- [Forum](http://forum.musicpd.org/)
- [IRC](irc://chat.freenode.net/#mpd)
- [Bug tracker](https://github.com/MusicPlayerDaemon/MPD/issues/)

# Developers

- [Protocol specification](http://www.musicpd.org/doc/protocol/)
- [Developer manual](http://www.musicpd.org/doc/developer/)

# Legal

MPD is released under the
[GNU General Public License version 2](https://www.gnu.org/licenses/gpl-2.0.txt),
which is distributed in the COPYING file.
