# Server: install the companion next to Jellyfin

**English** · [Русский](../документация/01-сервер.md)

The companion runs on the same machine as Jellyfin. That is not a technical
requirement — it talks to Jellyfin over HTTP like any other client — but it is
the simple choice: the video never leaves the machine before it is transcoded,
and you get `ffmpeg` for free, because Jellyfin ships one.

It does **not** read your media files. It asks Jellyfin for the video, with a
token, exactly as a browser would. So the account the service runs under needs
no permissions on your media folders at all, not even read.

## What the machine needs

* **Python 3.8 or newer.** An unpacked embeddable build is fine; a system-wide
  install is not required. See the note at the bottom if you use the
  embeddable one.
* **`ffmpeg.exe` and `ffprobe.exe` — both.** Jellyfin already has them in
  `C:\Program Files\Jellyfin\Server\`.

  `ffprobe` is not optional. It is what reads the real frame rate of a
  recording. Guessing it is a trap we fell into: a file was 24 fps, we assumed
  25, and ffmpeg dutifully inserted 155 duplicated frames — one per second. On
  the phone that looked like a hiccup every second, and it took a long time to
  find.
* Permission to read the program folder, write to its log folder, and use the
  network. Nothing else.

## Files

| File | What it is |
|---|---|
| `rtsp-proba.py` | the RTSP server: launches ffmpeg, parses URLs, checks tokens |
| `rtp_relay.py` | the relay: rewrites packet numbers and timestamps, reads player reports |
| `sdp-shablon-24.txt` | a ready-made stream description for 24 fps |
| `zapusk.cmd` | every setting lives here; this is what you wrap in a service |
| `token.txt` | **create this yourself**, one line: `TOKEN=<your Jellyfin token>` |

No dependencies beyond the Python standard library.

> The file and variable names are transliterated Russian — `zapusk` is "start",
> `proba` is "probe", `PROBA_EKRAN` is "screen". They are kept as they are
> because that is what the code says; renaming them would be a separate change.

## Install

1. Copy the companion folder somewhere on the Jellyfin machine, e.g.
   `C:\jellyfin-companion\`.
2. Get a Jellyfin API token: **Dashboard → API keys → new key**. Put it in
   `token.txt` as `TOKEN=...`.
3. Open `zapusk.cmd` and set the paths and the screen size (below).
4. Run `zapusk.cmd`. The log should say the server started on its port.
5. Set it up as a service so it survives a reboot (below).

## Settings

All of them live in `zapusk.cmd`. There are no addresses in the source.

| Variable | What it sets |
|---|---|
| `PROBA_PRODUKT=1` | don't build the experimental variants from sample files |
| `PROBA_JELLYFIN` | Jellyfin's address; on the same machine, `http://127.0.0.1:8096` |
| `PROBA_FFMPEG`, `PROBA_FFPROBE` | full paths — they are not on `PATH` |
| `PROBA_TOKEN_FILE` | the file holding `TOKEN=` |
| `PROBA_ZHURNALY` | log folder |
| `PROBA_PORT` | RTSP port. **Use 8554, not 554** — see below |
| `PROBA_EKRAN` | default screen size when the app doesn't name one |
| `PROBA_VIDEO_PKT` | video packet size limit, default 1100 bytes |
| `PROBA_ZAPOLNIT` | `1` fills the screen and crops, `0` keeps the whole frame with bars |
| `PROBA_PORTS_FROM`, `PROBA_PORTS_TO` | UDP range, default 40000–40063 |
| `PROBA_STROGO=1` | require a token from everyone, including your own LAN |

### Use port 8554, not 554

554 is the standard RTSP port, and that is exactly the problem: consumer
routers often have an "RTSP helper" or ALG sitting on it that intercepts the
conversation to "fix" it. Forwarding 554 then silently fails — the rule looks
identical to the ones that work, and from outside the port behaves as if it
were never forwarded.

Use 8554 on the server and forward that. The app lets you set the port, so
nothing is lost.

### `PROBA_EKRAN` and screen sizes

The app tells the server its own screen as part of the URL — `/s320x240`. The
companion sizes the picture and the bitrate from that: about 200 kbit/s for
320x240, 600 kbit/s for the E90's 800x352. Sending a large picture to a small
screen just spends bits, and on a mobile link bits are paid for in losses, not
in sharpness.

`PROBA_EKRAN` is the fallback for players that arrive at a bare URL and for app
builds old enough not to name their screen. **Set it to `320x240`** unless all
your phones are E90s — that is the size nearly every one of these devices has.

### About `sdp-shablon-24.txt`

A stream description (SDP) cannot be invented: it contains the decoder
configuration string that only the encoder can produce. Generating it means
opening the recording, transcoding a few seconds and reading the result — tens
of seconds while the phone sits waiting.

But it depends only on the picture size and the frame rate, both of which are
ours to choose. We verified this byte-for-byte. So the description is built
once per (frame rate, screen) pair and cached beside the program as
`sdp-shablon-<fps>-<WxH>.txt`. A 24 fps template ships with the project because
it is the common case; anything else is built on first use, once.

This is what makes the thing usable at all. Without it every episode would cost
tens of seconds of waiting.

## Ports

| What | Port |
|---|---|
| RTSP conversation | TCP 8554 |
| stream and reports | UDP 40000–40063 |

The UDP range is deliberately narrow so it can be forwarded as a block. Sixty
four ports is sixteen simultaneous sessions — each session uses four (data and
reports, for each of the two tracks).

Jellyfin's own port needs to be reachable too, since the app talks to it
directly, but the companion does not touch that.

## Who gets in

Recordings are served only to a caller holding a Jellyfin token, which the app
puts in the URL:

```
rtsp://<machine>:8554/jf/<item>/k<token>/s320x240/at<seconds>
```

The companion validates it by asking Jellyfin (`/Users/Me`) and remembers the
answer for five minutes. Without a valid token the request is refused at
`DESCRIBE`, before ffmpeg is ever started.

That ordering is the point. Every request starts a transcode, so an unchecked
endpoint means a dozen requests from the open internet can flatten the machine.

If Jellyfin is unreachable, nobody gets in. Its going down must not turn into
an open door.

There is one concession: requests from private ranges (`127.`, `10.`,
`192.168.`, `172.16–31.`) are allowed without a token, for old app builds.
There is never a concession from outside. Turn it off with `PROBA_STROGO=1`.

## Run it as a service

Any service wrapper works — NSSM, `sc.exe`, Task Scheduler at boot. Point it at
`zapusk.cmd`, not at `python.exe`: all the configuration is in that file.

Set it to restart on failure. The companion is not supposed to die, but a
transcoder that cannot start should not take the whole thing down until you
notice.

## Checking it is alive

The logs are in `PROBA_ZHURNALY`:

* `rtsp.log` — the whole conversation: who connected, what they asked for, how
  many packets went out, what the player complains about.
* `rtsp-ffmpeg.err` — ffmpeg's own output. Look here if the log says ffmpeg
  died immediately.

Lines worth knowing:

* `от плеера пришло: 40000=3, 40001=335` — "received from player". Even ports
  carry data. Zeros on the even ports while working from outside mean the
  return path never opened.
* `ОТКАЗ: ... без годного токена` — "refused, no valid token".
* `посредник N: пакет с ЧУЖОГО адреса` — a packet from an unexpected address:
  either someone's mistake or an attempt to redirect the stream.
* `ПОТОК СТОИТ: за полминуты ни одного пакета` — "stream stalled": ffmpeg is
  alive but nothing is reaching the phone.
* `отдано: видео N с, звук M с, расхождение ±X с` — how far each track has been
  delivered. `расхождение` ("divergence") is the lead of audio over video in
  *delivery*, not a lip-sync error; it sits around half a second and should
  stay flat. If it grows, something is wrong on our side.

### What goes back to Jellyfin

The companion, not the app, tells Jellyfin where you are. It has to be this
way: the app hands a URL to the system player and walks away — it never learns
about pauses or about you leaving the player. The companion sees `PAUSE` and
`TEARDOWN` and knows exactly how many seconds of the recording it has sent.

It reports **as the person watching**, using the token the app puts in the
address, not the token in `token.txt`. Jellyfin keeps a position in the card
of whichever user the report arrived as, so reporting as a service user would
file your minutes under a user nobody looks at — the position would be saved
and invisible at the same time. `token.txt` stays as the fallback for older
app builds that send no token. Which one was used is in the log: `(ответ 204,
токен зрителя)` or `(ответ 204, служебный токен)`.

Three reports, and a line in the log for each:

* `Jellyfin: начал смотреть <id> с N с` — playback started. This is what makes
  the phone show up in Jellyfin's device list and activity feed.
* progress, every 30 seconds while the stream runs. Silent in the log on
  purpose: a line every half minute would bury everything else. Its point is
  that a dead phone link loses at most 30 seconds of your position instead of
  the whole episode.
* `Jellyfin: отметил N с по записи <id> (ответ 204)` — the final position.

Two lines explain a report that did *not* happen:

* `не отмечаю: за запуск отдано N с -- это не просмотр` — under 20 seconds was
  delivered, so this was a mis-press, and marking it would overwrite a good
  position with a useless one.
* `поправка на молчание: отдано N с, плеер молчит M с` — the player stopped
  acknowledging M seconds ago while ffmpeg kept feeding the stream into
  nothing, so M is subtracted. Without this, a dropped connection moved your
  position two minutes further than you ever watched.

The "watched" tick is Jellyfin's own decision, from the final position against
the runtime — the companion does not set it.

## If you use the embeddable Python

That build deliberately leaves the script's own folder out of the module search
path — the `python38._pth` file next to `python.exe` controls this. So
`from rtp_relay import ...` fails with `ModuleNotFoundError` even though the
file is right there.

Add the companion's folder as a line in `python38._pth`. A normal Python
install does not have this problem.
