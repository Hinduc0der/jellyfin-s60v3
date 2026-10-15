# When it doesn't work

**English** · [Русский](../документация/04-если-не-работает.md)

## Start with the log

Almost every question is answered in `rtsp.log` in the companion's log folder.
Open it and look at the last session from the bottom up.

The lines are in Russian; the ones that matter are listed at the end of
[the server page](01-server.md#checking-it-is-alive).

## "Connection not possible" on the phone

The app never reached Jellyfin — this is before any video is involved.

1. Is the address right, and does it include the port? `192.168.1.50:8096`.
2. Open the same address in a browser on another device.
3. From outside: is Jellyfin's port forwarded, and is your WAN address actually
   public? See [Network](02-network.md).

If the library loads but video does not start, this is not your problem — skip
to the next section.

## The player thinks, and nothing happens

The conversation worked and the stream never arrived.

Look in the log for `от плеера пришло:` ("received from player") followed by
port numbers. **Even ports are the data paths.**

* Even ports show zero, odd ports show counts → the return path never opened.
  The UDP range is not forwarded, or not forwarded to the right machine.
* Nothing at all → the player never got as far as setting up. Check the RTSP
  port forward, and check you are not on 554 (see [Network](02-network.md)).

Also check `rtsp-ffmpeg.err`. If the log says ffmpeg died immediately, the
reason is there — usually a wrong path in `PROBA_FFMPEG`, or a token Jellyfin
no longer accepts.

## Sound but no picture

Nearly always the picture is arriving and being thrown away.

Look for `плеер сообщает: потеряно ...%` ("player reports N% lost"). If the
loss is high, the link is dropping packets and there is nothing the server can
do — RTP does not retransmit.

If loss is near zero and the picture is still missing, check that the screen
size in the URL matches reality. A picture far larger than the screen can
exceed what the decoder will accept.

## The picture breaks into blocks

Short bursts of blocks that recover within a second or two are packet loss on
the link. You will see them in the log as a single interval with high loss.
Mobile links do this; nothing to fix.

Blocks that persist, or appear constantly, used to mean something different:
video packets being fragmented. The companion caps the video packet at 1100
bytes (`PROBA_VIDEO_PKT`), which keeps the IP packet at 1128 — small enough to
pass whole on mobile networks, where the limit is usually 1400–1430 rather than
the 1500 of a normal network. A fragmented packet loses its whole payload if
either half goes missing, and the picture stays broken until the next keyframe.

If you raised that value, put it back.

## The list is empty

The app signed in but Jellyfin returned nothing. Usually the user has no
libraries assigned, or the token belongs to a user that does not. Check in
Jellyfin under the user's access settings.

## It worked, and then stopped

Look at the end of the last session in the log.

* `разговор молчал две минуты -- закрываю сессию` ("the conversation went quiet
  for two minutes; closing") plus `молчат отчёты: ...=118 с` means the phone
  stopped talking entirely — its link died. The server was still sending. The
  player reconnects by itself and resumes.
* If the reports line shows only a few seconds of silence while the
  conversation shows 120, that would be the server closing a live session too
  early. That is a bug; report it with those numbers.

Mobile links drop. The session closes, the player reconnects and asks to
continue from where it was. A few seconds of interruption is the expected
behaviour, not a fault.

## Audio out of sync

Should not happen any more. If it does, the useful number is in the log:

```
отдано: видео 480.2 с, звук 480.3 с, расхождение +0.10 с
```

`расхождение` is how much further the audio has been *delivered* than the
video. Around half a second is normal and harmless — it is the gap between
ffmpeg's two outputs, not a lip-sync error.

What matters is whether it **grows**. A number that climbs with every pause is
a real fault; a number sitting still is not. Include a few consecutive lines if
you report it.

Do not judge this by watching lips: half a second and two seconds look much
the same, and only the number in the log tells them apart.

## Getting help

Include:

* the last session from `rtsp.log` — from `--- подключился` to
  `--- отключился`;
* the matching tail of `rtsp-ffmpeg.err`;
* the phone model, the app version, and whether you were at home or outside.

The logs contain your Jellyfin item IDs and the IP address you connected from.
Strip them if you would rather not share them.
