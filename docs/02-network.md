# Network: watching from outside your home

**English** · [Русский](../документация/02-сеть.md)

At home you need none of this. The phone and the server are on the same
network, and it simply works.

This page is about the other case: watching over mobile data, or from someone
else's Wi-Fi.

## What you must have

**A public IP address.** If your provider puts you behind carrier-grade NAT —
you share one address with other subscribers — no amount of port forwarding
will help, because there is no address to forward from. Many providers will
give you a static or at least a public address on request, sometimes for a
small fee.

How to tell: look at the WAN address in your router's status page. If it starts
with `100.64.`–`100.127.`, or is in `10.`, `192.168.` or `172.16–31.`, you are
behind the provider's NAT.

**A way to reach your address.** A static IP is simplest. A dynamic one works
with any dynamic-DNS service; the app takes a hostname just as happily as an
address.

## What to forward

| Protocol | External | Internal | To |
|---|---|---|---|
| TCP | 47231 (any unusual number) | 8554 | the Jellyfin machine |
| UDP | 40000–40063 | 40000–40063 | the Jellyfin machine |
| TCP | 8096 (or yours) | 8096 | the Jellyfin machine |

The third one is Jellyfin itself: the app fetches the library from it directly,
not through the companion.

Forward the UDP range as a block. That is exactly why the companion restricts
itself to sixty-four ports instead of letting the system pick freely — you
cannot forward a port chosen at random out of sixty thousand.

### Do not use port 554

554 is the standard RTSP port, and consumer routers often run an "RTSP helper"
(ALG) on it that intercepts the conversation to rewrite it. When that happens,
your forwarding rule silently does nothing: it looks identical to rules that
work, and from outside the port behaves as though it were never forwarded at
all.

Keep 554 out of it on both sides. If your router has an RTSP ALG setting, turn
it off too.

## Checking that it works

Checking the RTSP port with a plain connection tells you almost nothing: the
conversation may well succeed while the stream never arrives, because the
stream uses different ports that only exist while a session is live.

Two useful checks:

1. **From outside, on the phone.** The real test, and the slowest.
2. **The server log.** Look for `от плеера пришло:` ("received from player")
   followed by port numbers. The even ports are the data paths. If the odd
   ports have counts and the even ones are zero, the conversation works and the
   stream path does not — that is a forwarding problem, not a server problem.

## What not to do

* **Don't put the companion behind a reverse proxy.** nginx and friends speak
  HTTP; this is RTSP with UDP streams beside it. There is nothing for a proxy
  to proxy.
* **Don't forward "all ports" to the machine.** You need three rules, and
  Jellyfin's web interface facing the open internet is a real thing to think
  about, not a formality.
* **Don't expect UPnP to do it.** It does not know about the UDP range.

## If you open a port to the internet

You are putting two services on the public internet: Jellyfin and the
companion. Worth knowing:

* The companion refuses anything without a valid Jellyfin token **before**
  starting a transcode. That is deliberate — otherwise a handful of anonymous
  requests would start a handful of transcodes and the machine would fall over.
  It also refuses anything that is not a library item at all, which is what
  the scanners ask for.
* **Pick an unusual port number.** 554 and 8554 are the standard RTSP ports,
  and bots sweep them: within a day of opening 8554 you will see requests for
  `/cam/realmonitor` and `/h264/ch01/main/av_stream` — scanners looking for
  security cameras. They get refused, but a high random port such as 47231
  means they never find you in the first place. Outside and inside do not
  have to match: the companion can keep listening on 8554 while the forward
  uses something nobody sweeps. The outside number is the one that goes in the
  app, under Settings → Stream port.
* The token travels inside the RTSP URL, in the clear. RTSP has no TLS here and
  the phone could not do it anyway. Treat that token as something that can
  leak: make a dedicated API key for this, and revoke it if you stop using it.
* Jellyfin's own port is the bigger exposure of the two. Keep it updated, and
  give the account you use a password you do not use anywhere else.

If that is more than you want to expose, run it over a VPN into your home
network instead and skip the forwarding entirely. The phone cannot do VPN, but
your router may be able to.

## How the phone gets in at all

This part is interesting, and explains a design choice you will see in the
logs.

The player cannot receive the stream inside the TCP connection — we tested
three ways of doing that and it simply hangs up. So the stream has to arrive as
UDP, on ports the phone opened. Behind someone else's router, an incoming
packet to a port the phone never sent from is dropped: the router has no idea
who it belongs to.

The player handles this the way a 3GPP client should: right after setting up a
track, and **before the stream starts**, it sends a packet out from the very
port it is about to listen on. That packet opens the return path in whatever
router it is behind.

So the companion replies not to the port the player announced for itself, but
to the port the packet actually came from. Only the port is taken from the
incoming packet, and only when the address matches the one from the RTSP
conversation — otherwise a single forged packet could redirect somebody's video
stream to a machine that never asked for it.
