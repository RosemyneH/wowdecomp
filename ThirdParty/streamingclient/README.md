# Streaming client (GPL-2.0)

Play-while-downloading **WoW 3.3.5a (12340)** data over HTTP, using the `WoW.mfil` layout documented by **brian8544**.

| Piece | Where |
|---|---|
| Protocol and client setup | [brian8544/streamingclient-3.3.5](https://github.com/brian8544/streamingclient-3.3.5) |
| CDN / worldserver HTTP range server | [brian8544/mod-streamingclient](https://github.com/brian8544/mod-streamingclient) |
| Vendored server sources | `upstream/` in this directory |
| License | `COPYING` (GNU GPL version 2) |

**Copyright brian8544.** The files under `upstream/` are his AzerothCore module, included verbatim so the GPL-2.0 work and its copyright travel with this tree. That module is a server. It is not compiled into `WowClient`.

This client already loads `WoW.mfil` at startup (`LoadMFIL` in `WoW/Source/Client.cpp`) and fills missing MPQ bytes with HTTP `Range` requests, cached as `.part` files under `Data/`. A manifest with `version=1`, `source=http://yourcdn.com/`, and `Data/...MPQ;size;12340;0` lines is enough. `sourcemanifest` and `transportmanifest` are optional, which is the layout his CDN documents.

A starter `WoW.mfil.example` sits next to this file. The Android app writes that file into app storage on first launch when one is not already there. Point `source=` at a CDN that serves the 3.3.5a `Data/` tree and accepts range requests.

Because `upstream/` is GPL-2.0, distributing those sources requires offering them under GPL-2.0. This directory is that offer.
