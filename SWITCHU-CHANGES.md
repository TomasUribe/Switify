# Changes in this copy (SwitchU)

Modified copy of Switify for use with the SwitchU HOME menu. GPL-3.0-or-later,
like the original. Only the background service needs to be replaced.

- **New IPC command `ReloadCurrent` (39).** After a console sleep it waits for
  Spotify's session to be back, then starts the current song from a fresh stream at
  its position (`background_backend.cpp`, `spotify_engine.cpp`). SwitchU sends it on
  wake, so a sleep does not skip the song.
- **What was loaded plays while it reconnects.** On `ReloadCurrent` the paused song
  resumes from the old stream's buffers. When they run out, cspot holds the song
  (`holdOnStreamLoss`, `TrackPlayer.cpp`) instead of ending it; after the decoded
  tail plays, the song pauses there and the reload starts it at that point. Status
  flag `SwitifyStatusFlag_Reloading` (bit 8) is set meanwhile, for SwitchU's
  "Reconnecting..." label. Play on a held song asks for the reload again.
- **Read-ahead, and the same stream reads on.** `CDNAudioFile` keeps at least 192 KiB
  downloaded ahead (about 4.8 s at 320 kbps), so a sleep always leaves that much
  to play. While the connection is known to be down it makes no requests. Once
  Spotify is online again the held stream reads on from the byte it stopped at:
  no jump, and no gap if it came back in time. Only if that fails (an expired
  address) is the song reloaded, at the decoder's position (`ov_time_tell`), not
  the clock's.
- **A bad CDN answer drops the connection.** Only a 206 starting at the byte asked
  for is used; anything else (or a failure) replaces the kept-alive connection. An
  unread body left on it after a wake made every later request fail, and the song
  ended. An end far from the file's end counts as a lost stream, not the song's
  end. Songs stay held this way until 60 s after the connection came back.
- **Skipping fast no longer crashes it.** The session hands a track's metadata or
  audio key over outside its lock, so a track skipped past in the meantime was
  freed under the answer (`free` in `QueuedTrack::stepParseMetadata`). Those
  answers now hold the track through a `weak_ptr` and drop it if it is gone
  (`TrackQueue.cpp`).
- **New IPC command `GetAudioLevels` (38).** Returns 32 band levels of what is
  playing, for SwitchU's bars. They are computed only when requested, so nothing
  runs while a game plays unless a client asks (`switify_service.cpp`,
  `NXAudioSink.cpp`, `switify/spectrum_analyzer.hpp`).
- **Gaps no longer change the song.** After a wake the decoder could report a gap
  (`OV_HOLE`), which ended the song. A few in a row are skipped; a stream of nothing
  but gaps still stops (`TrackPlayer.cpp`).
- **CDN error pages are not decoded as audio.** 401/403/404/410 end the song at once;
  other non-2xx answers and unusable lengths end the read (`CDNAudioFile.cpp`).

- **The heap is taken as needed.** It was a fixed 4 MiB static array, counted in
  full against the system's memory from the start (the service runs in the System
  resource-limit group even with the Applet pool). With little left to the system,
  opening HOME over a game while this service ran exhausted it: error reports showed
  `SystemPhysicalMemoryPeak` equal to its limit, FS sessions closed and the menu's
  GPU start hung. `_sbrk_r` now commits heap in 2 MiB steps up to the same ceiling
  (`engine/main.cpp`): about 2 MiB less in normal use.
- **The service counts as an applet** (`application_type: 2` in
  `switify-sysmodule.json`). The heap still crossed 2 MiB after a while (free gaps
  it never returns), and HOME failed again. As an applet, pm puts it in the Applet
  resource-limit group, beside SwitchU's daemon and menu, which has tens of MiB to
  spare; the system's memory no longer pays for it at all. It creates no kernel
  events, so that group's event limit does not apply.

Removed again: a failed CDN read used to be retried every second for 90 s, and a
stream open 24 times. Each retry was a TLS handshake on the system core. When a game
launched (which drops Switify's connection) that load starved FS, graphics and audio:
HOME could not open over the game, and the console could freeze entering it. Reads now
fail like the official build; `ReloadCurrent` covers the sleep case. The nifm session
the reload used is gone too (Spotify's session state is enough). Pool and FS sessions
are the official ones.

Based on tag v0.7.0, the source of the official service (Overlay 0.5.0). Use devkitA64 r30
(GCC 16), like the official build: a GCC 15 build stalled on the console when requesting a
song, and comparing the binaries showed the compiler as the difference.

Build the service with the devkitPro Docker image from the repository root:

    cmake -S phase2/sysmodule -B phase2/build_engine -G Ninja -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake -DCMAKE_BUILD_TYPE=Release
    ninja -C phase2/build_engine switify-background-package

Install `phase2/build_engine/switify-background.nsp` as
`sdmc:/atmosphere/contents/4200000000535754/exefs.nsp`.
