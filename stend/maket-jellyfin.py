# -*- coding: utf-8 -*-
"""Сервер-макет Jellyfin: ровно те запросы, которые делает наш клиент.

Нужен для скриншотов документации. В ответах -- выдуманная библиотека из
общественного достояния, чтобы на картинках не было ни одного файла владельца
и ни одного его адреса.

Запуск:  python maket-jellyfin.py [порт]   (по умолчанию 8099)
"""
import io
import json
import os
import struct
import sys
import zlib

try:
    from http.server import BaseHTTPRequestHandler, HTTPServer
    from urllib.parse import urlparse, parse_qs
except ImportError:                                   # Python 2
    from BaseHTTPServer import BaseHTTPRequestHandler, HTTPServer
    from urlparse import urlparse, parse_qs

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8099
# Язык названий в выдуманной библиотеке. Нужен затем, что английская
# документация не должна показывать русские разделы: названия разделов и
# серий приходят с сервера, а не из приложения.
YAZYK = sys.argv[2] if len(sys.argv) > 2 else "ru"


def sl(ru, en):
    return en if YAZYK == "en" else ru

TICK = 10000000  # тиков в секунде у Jellyfin


def vid(name, kind, **extra):
    one = {"Name": name, "Id": "%08x" % (abs(hash(name)) & 0xFFFFFFFF),
           "Type": kind}
    one.update(extra)
    return one


RAZDELY = [
    vid(sl("Сериалы", "TV Shows"), "CollectionFolder", CollectionType="tvshows"),
    vid(sl("Фильмы", "Movies"), "CollectionFolder", CollectionType="movies"),
    vid(sl("Музыка", "Music"), "CollectionFolder", CollectionType="music"),
]

SERIALY = [
    vid("Night of the Living Dead", "Series", ProductionYear=1968),
    vid("Plan 9 from Outer Space", "Series", ProductionYear=1959),
    vid("The Lost World", "Series", ProductionYear=1925),
]

SEZONY = [vid(sl("Сезон 1", "Season 1"), "Season"), vid(sl("Сезон 2", "Season 2"), "Season")]

SERII = [
    vid(sl("Пробуждение", "Awakening"), "Episode", IndexNumber=1, ParentIndexNumber=1,
        SeriesName="Night of the Living Dead", RunTimeTicks=24 * 60 * TICK),
    vid(sl("Долгая ночь", "The Long Night"), "Episode", IndexNumber=2, ParentIndexNumber=1,
        SeriesName="Night of the Living Dead", RunTimeTicks=26 * 60 * TICK,
        UserData={"PlaybackPositionTicks": 11 * 60 * TICK}),
    vid(sl("Рассвет", "Daybreak"), "Episode", IndexNumber=3, ParentIndexNumber=1,
        SeriesName="Night of the Living Dead", RunTimeTicks=25 * 60 * TICK,
        UserData={"Played": True}),
]

FILMY = [
    vid("The General", "Movie", ProductionYear=1926,
        RunTimeTicks=78 * 60 * TICK),
    vid("Nosferatu, eine Symphonie des Grauens", "Movie", ProductionYear=1922,
        RunTimeTicks=94 * 60 * TICK,
        UserData={"PlaybackPositionTicks": 37 * 60 * TICK}),
]

ALBOMY = [
    vid("Public Domain Classics", "MusicAlbum"),
    vid("Field Recordings 1908", "MusicAlbum"),
]

PESNI = [
    vid("Hungarian Dance No. 5", "Audio", IndexNumber=1,
        AlbumArtist="Johannes Brahms", Album="Public Domain Classics",
        RunTimeTicks=(2 * 60 + 18) * TICK),
    vid("Ride of the Valkyries", "Audio", IndexNumber=2,
        AlbumArtist="Richard Wagner", Album="Public Domain Classics",
        RunTimeTicks=(5 * 60 + 2) * TICK),
    vid("Clair de Lune", "Audio", IndexNumber=3,
        AlbumArtist="Claude Debussy", Album="Public Domain Classics",
        RunTimeTicks=(5 * 60 + 21) * TICK),
    vid("Gymnopédie No. 1", "Audio", IndexNumber=4,
        AlbumArtist="Erik Satie", Album="Public Domain Classics",
        RunTimeTicks=(3 * 60 + 25) * TICK),
    vid("The Entertainer", "Audio", IndexNumber=5,
        AlbumArtist="Scott Joplin", Album="Public Domain Classics",
        RunTimeTicks=(4 * 60 + 44) * TICK),
    vid("Also sprach Zarathustra, Einleitung", "Audio", IndexNumber=6,
        AlbumArtist="Richard Strauss", Album="Public Domain Classics",
        RunTimeTicks=(1 * 60 + 47) * TICK),
]

# Альбом у песен -- чтобы обложка просилась одна на всех.
for p in PESNI:
    p["AlbumId"] = ALBOMY[0]["Id"]


def png_oblozhka(storona=96):
    """Одноцветная картинка -- чтобы на скриншоте было видно место обложки."""
    ryad = b"\x00" + (b"\x2b\x3a\x52" * storona)
    syroe = ryad * storona
    def kusok(imya, telo):
        return (struct.pack(">I", len(telo)) + imya + telo
                + struct.pack(">I", zlib.crc32(imya + telo) & 0xFFFFFFFF))
    return (b"\x89PNG\r\n\x1a\n"
            + kusok(b"IHDR", struct.pack(">IIBBBBB", storona, storona, 8, 2, 0, 0, 0))
            + kusok(b"IDAT", zlib.compress(syroe))
            + kusok(b"IEND", b""))


class Maket(BaseHTTPRequestHandler):
    def otdat(self, telo, tip="application/json"):
        self.send_response(200)
        self.send_header("Content-Type", tip)
        self.send_header("Content-Length", str(len(telo)))
        self.end_headers()
        self.wfile.write(telo)

    def spisok(self, items):
        telo = json.dumps({"Items": items, "TotalRecordCount": len(items)},
                          ensure_ascii=False).encode("utf-8")
        self.otdat(telo)

    def do_GET(self):
        put = urlparse(self.path)
        zapros = parse_qs(put.query)

        if put.path == "/UserViews":
            return self.spisok(RAZDELY)
        if put.path == "/UserItems/Resume":
            return self.spisok([SERII[1], FILMY[1]])
        if put.path.startswith("/Items/") and put.path.endswith("/Primary"):
            return self.otdat(png_oblozhka(), "image/jpeg")
        if put.path.startswith("/Audio/"):
            # Тишина: для скриншотов звук не нужен, важно лишь, что файл есть.
            return self.otdat(b"\xff\xfb\x90\x00" + b"\x00" * 64000, "audio/mpeg")
        if put.path == "/Items":
            vid_zaprosa = (zapros.get("IncludeItemTypes") or [""])[0]
            roditel = (zapros.get("ParentId") or [""])[0]
            if vid_zaprosa == "Series":
                return self.spisok(SERIALY)
            if vid_zaprosa == "Movie":
                return self.spisok(FILMY)
            if vid_zaprosa == "MusicAlbum":
                return self.spisok(ALBOMY)
            if roditel in (a["Id"] for a in ALBOMY):
                return self.spisok(PESNI)
            if roditel in (s["Id"] for s in SERIALY):
                return self.spisok(SEZONY)
            return self.spisok(SERII)

        self.send_response(404)
        self.end_headers()

    def log_message(self, *args):
        pass


if __name__ == "__main__":
    print("макет Jellyfin на http://127.0.0.1:%d" % PORT)
    HTTPServer(("127.0.0.1", PORT), Maket).serve_forever()
