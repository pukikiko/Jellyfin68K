This folder is shared with the emulated Mac as the "Unix" volume.

Jellyfin 68K reads its configuration from Unix:Jellyfin68K.cfg.  `make run`
creates that file from examples/Jellyfin68K.cfg on first use.  The emulator
configuration uses host=10.0.2.2 (the slirp gateway and relay); a real Mac on
the LAN should use host=10.0.1.1 or the server's actual address.

The Settings window (Cmd-,) can change the instance, username, password, and
quality.  It writes the changes back here (or to System Folder:Preferences on
a real Mac).  Quality values are low, standard, and high.

The client writes this diagnostic file here:
  jellyfin.log   login, library, search, and streaming playback log

Playback streams PCM from the server straight to the Sound Manager, so the
client does not create jf.mp3 or jf.aiff.  Files left over from older builds
can be deleted.

The log is ignored by git and is overwritten at each launch.
