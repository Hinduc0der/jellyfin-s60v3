# Notes for the Nokia E90

**English** · [Русский](../документация/05-E90.md)

The E90 Communicator is the device this was built on, and it is unusual enough
to deserve its own page. Everything here is extra; the rest of the project does
not assume it.

## Two screens

The E90 has an outer screen of 240x320 and, under the lid, an inner one of
800x352. The app runs on whichever is active.

It remembers the **largest screen it has seen** while running, and tells the
server that one. Otherwise opening the lid halfway through would leave you
watching a picture built for a third of the screen.

If you only ever use the outer screen, the server's `PROBA_EKRAN` fallback and
the usual 320x240 advice apply to you, not this page.

## Picture

800x352 at about 600 kbit/s. That is roughly 270 MB per hour.

The screen is 2.27:1 — wider than any common content. There is no point
encoding more than it can show, and no point spending bitrate a 332 MHz CPU
cannot decode in time.

## The black bars are the other way round

On a normal 4:3 phone screen, 16:9 content gets bars above and below. On the
E90 it is the opposite: the screen is *wider* than the content, so a 16:9 frame
fitted to the height leaves bars on the **left and right**.

This surprised us enough to investigate the player for a day. It is not the
player: the frame is 1280x720, the screen is 800x352, and fitting the whole
frame inside it gives 626x352 with padding at the sides. "Full screen" in the
player does not change this, because the padding is already part of the picture
we send.

If you would rather fill the screen and lose the top and bottom of the frame,
set `PROBA_ZAPOLNIT=1` on the server. The default keeps the whole frame.

## What not to expect

* **No seeking during playback.** This is the built-in player, not us. Choose
  the position in the app before you start.
* **No subtitles, no audio track selection.**
* **No 4:3 content filling the screen.** It will have very wide bars. That is
  correct; a 2.27:1 screen is simply much wider than 4:3.
* **Going full screen requires a pause.** The player works that way. The server
  handles the pause-and-resume cleanly — it resumes exactly where it stopped,
  without repeating audio — but there is a second or two of rebuffering.

## Why "connection not possible" shows up on the E90 specifically

Usually the access point. The E90 will happily have several — Wi-Fi, two
operator profiles — and the app and the player each choose one. If the app
reaches Jellyfin over Wi-Fi while the player goes out over mobile data, the
player ends up outside your network trying to reach a LAN address.

Pin the player's access point: *Options → Settings → Streaming → Access point*.
This also stops it asking the question on every launch.
