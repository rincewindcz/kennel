#!/usr/bin/env python3
"""Generates kennel-read test books from a Project Gutenberg UTF-8 text.

  mkbooks.py fb2  TXT OUT     FictionBook 2 with sections, titles and emphasis
  mkbooks.py pdb  TXT OUT     PalmDOC (TEXtREAd), PalmDOC compression
  mkbooks.py pdb0 TXT OUT     PalmDOC, uncompressed
  mkbooks.py mobi TXT OUT     MOBI (BOOKMOBI), uncompressed, UTF-8 HTML, no EXTH

Chapters are lines starting with "Chapter" or "CHAPTER"; paragraphs are blank-line separated
blocks; _underscored_ words become emphasis. Text outside the Gutenberg START/END markers is dropped.
"""
import html
import re
import struct
import sys
import time

RECORD = 4096


def parse(path):
    text = open(path, encoding="utf-8-sig").read().replace("\r\n", "\n")
    start = re.search(r"^\*\*\* ?START OF.*$", text, re.M)
    end = re.search(r"^\*\*\* ?END OF.*$", text, re.M)
    if start and end:
        text = text[start.end():end.start()]
    title = re.search(r"^Title: (.*)$", open(path, encoding="utf-8-sig").read(), re.M)
    author = re.search(r"^Author: (.*)$", open(path, encoding="utf-8-sig").read(), re.M)
    chapters = [["", []]]
    for block in re.split(r"\n\s*\n", text):
        para = " ".join(line.strip() for line in block.strip().splitlines())
        if not para:
            continue
        if re.match(r"^(Chapter|CHAPTER)\b", para) and len(para) < 80:
            chapters.append([para, []])
        else:
            chapters[-1][1].append(para)
    if not chapters[0][1]:
        chapters.pop(0)
    return (title.group(1).strip() if title else "Untitled"), (author.group(1).strip() if author else ""), chapters


def emphasis(para, open_tag, close_tag):
    return re.sub(r"_([^_]+)_", lambda m: open_tag + m.group(1) + close_tag, html.escape(para, quote=False))


def write_fb2(src, out):
    title, author, chapters = parse(src)
    first, _, last = author.rpartition(" ")
    parts = ['<?xml version="1.0" encoding="UTF-8"?>',
             '<FictionBook xmlns="http://www.gribuser.ru/xml/fictionbook/2.0">',
             "<description><title-info>",
             f"<author><first-name>{html.escape(first)}</first-name><last-name>{html.escape(last)}</last-name></author>",
             f"<book-title>{html.escape(title)}</book-title><lang>en</lang>",
             "</title-info></description>", "<body>",
             f"<title><p>{html.escape(title)}</p></title>"]
    for name, paras in chapters:
        parts.append("<section>")
        if name:
            parts.append(f"<title><p>{html.escape(name)}</p></title>")
        for i, p in enumerate(paras):
            if i and i % 25 == 0:
                parts.append("<empty-line/>")
            parts.append("<p>" + emphasis(p, "<emphasis>", "</emphasis>") + "</p>")
        parts.append("</section>")
    parts += ["</body>", '<binary id="cover.jpg" content-type="image/jpeg">AAAA</binary>', "</FictionBook>"]
    open(out, "w", encoding="utf-8").write("\n".join(parts))


def palmdoc_compress(data):
    out = bytearray()
    i = 0
    n = len(data)
    chains = {}  # 3-byte prefix -> earlier positions

    def remember(pos):
        if pos + 3 <= n:
            chains.setdefault(data[pos:pos + 3], []).append(pos)

    while i < n:
        # Back reference: 3 to 10 bytes within the previous 2047.
        best_len, best_dist = 0, 0
        for pos in reversed(chains.get(data[i:i + 3], [])[-32:]):
            if i - pos > 2047:
                break
            length = 3
            while length < 10 and i + length < n and data[pos + length] == data[i + length]:
                length += 1
            if length > best_len:
                best_len, best_dist = length, i - pos
                if length == 10:
                    break
        if best_len >= 3:
            for k in range(best_len):
                remember(i + k)
            value = 0x8000 | (best_dist << 3) | (best_len - 3)
            out += struct.pack(">H", value)
            i += best_len
            continue
        c = data[i]
        if c == 0x20 and i + 1 < n and 0x40 <= data[i + 1] <= 0x7F:
            out.append(data[i + 1] ^ 0x80)
            remember(i)
            remember(i + 1)
            i += 2
            continue
        if c == 0 or 0x09 <= c <= 0x7F:
            out.append(c)
            remember(i)
            i += 1
            continue
        # Run of up to 8 bytes that need escaping.
        j = i
        while j < n and j - i < 8 and not (data[j] == 0 or 0x09 <= data[j] <= 0x7F):
            j += 1
        out.append(j - i)
        out += data[i:j]
        for k in range(i, j):
            remember(k)
        i = j
    return bytes(out)


def pdb(name, kind, creator, records):
    count = len(records)
    header = bytearray(78)
    header[0:32] = name.encode("utf-8")[:31].ljust(32, b"\0")
    now = int(time.time()) + 2082844800
    struct.pack_into(">HHIII", header, 32, 0, 0, now, now, 0)
    header[60:64] = kind
    header[64:68] = creator
    struct.pack_into(">H", header, 76, count)
    offset = 78 + 8 * count + 2
    entries = bytearray()
    for i, r in enumerate(records):
        entries += struct.pack(">IB", offset, 0) + (2 * i).to_bytes(3, "big")
        offset += len(r)
    return bytes(header) + bytes(entries) + b"\0\0" + b"".join(records)


def text_records(data, compress):
    chunks = [data[i:i + RECORD] for i in range(0, len(data), RECORD)]
    return [palmdoc_compress(c) if compress else c for c in chunks]


def write_pdb(src, out, compress):
    title, _, chapters = parse(src)
    body = []
    for name, paras in chapters:
        if name:
            body.append(name.upper())
        body += [p.replace("_", "") for p in paras]
    data = "\n\n".join(body).encode("utf-8")
    recs = text_records(data, compress)
    rec0 = struct.pack(">HHIHHI", 2 if compress else 1, 0, len(data), len(recs), RECORD, 0)
    open(out, "wb").write(pdb(title, b"TEXt", b"REAd", [rec0] + recs))


def write_mobi(src, out):
    title, author, chapters = parse(src)
    parts = ["<html><head><guide></guide></head><body>"]
    for name, paras in chapters:
        if name:
            parts.append(f"<h2>{html.escape(name)}</h2>")
        for p in paras:
            parts.append("<p>" + emphasis(p, "<i>", "</i>") + "</p>")
        parts.append("<mbp:pagebreak/>")
    parts.append("</body></html>")
    data = "".join(parts).encode("utf-8")
    recs = text_records(data, False)

    full_name = title.encode("utf-8")
    mobi = bytearray(0xE8)
    mobi[0:4] = b"MOBI"
    struct.pack_into(">IIIII", mobi, 4, 0xE8, 2, 65001, 0x4B4E4E4C, 6)
    for off in range(24, 64, 4):
        struct.pack_into(">I", mobi, off, 0xFFFFFFFF)
    rec0_len = 16 + 0xE8
    struct.pack_into(">IIII", mobi, 64, len(recs) + 1, rec0_len, len(full_name), 9)
    struct.pack_into(">IIIIIIIII", mobi, 80, 0, 0, 6, 0xFFFFFFFF, 0, 0, 0, 0, 0)
    struct.pack_into(">IIIII", mobi, 148, 0xFFFFFFFF, 0xFFFFFFFF, 0, 0, 0)
    struct.pack_into(">HHI", mobi, 176, 1, len(recs), 1)
    struct.pack_into(">IIII", mobi, 208, 0xFFFFFFFF, 0, 0xFFFFFFFF, 0xFFFFFFFF)
    struct.pack_into(">II", mobi, 224, 0, 0xFFFFFFFF)  # extra record data flags 0, no INDX
    rec0 = struct.pack(">HHIHHHH", 1, 0, len(data), len(recs), RECORD, 0, 0) + bytes(mobi) + full_name
    rec0 += b"\0" * (4 - len(rec0) % 4)
    open(out, "wb").write(pdb(title, b"BOOK", b"MOBI", [rec0] + recs))


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    kind, src, out = sys.argv[1:]
    if kind == "fb2":
        write_fb2(src, out)
    elif kind in ("pdb", "pdb0"):
        write_pdb(src, out, kind == "pdb")
    elif kind == "mobi":
        write_mobi(src, out)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
