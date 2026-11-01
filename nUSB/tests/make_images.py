#!/usr/bin/env python3
"""Create sparse, deterministic read-only test media and input-based manifests."""

import argparse
import json
from pathlib import Path
import struct
import subprocess


COMMIT = "ab992fcf2926b8a1ee3f0a8f9cefb6584174100d"
EOC = 0x0FFFFFFF
LONG_NAME = "Long filename example.txt"
DEEP_NAME = "d" * 240
MAX_NAME = "界" * 251 + ".txt"


def put16(buf, offset, value):
    struct.pack_into("<H", buf, offset, value)


def put32(buf, offset, value):
    struct.pack_into("<I", buf, offset, value)


def short_entry(alias, attr, cluster=0, size=0, lowercase=False):
    entry = bytearray(32)
    entry[:11] = alias.encode("ascii")
    assert len(entry) == 32 and len(alias) == 11
    entry[11] = attr
    entry[12] = 0x18 if lowercase else 0
    put16(entry, 20, cluster >> 16)
    put16(entry, 26, cluster & 0xFFFF)
    put32(entry, 28, size)
    return bytes(entry)


def long_entries(name, alias):
    units = list(struct.unpack("<%dH" % (len(name.encode("utf-16le")) // 2),
                               name.encode("utf-16le")))
    assert len(units) <= 255
    checksum = 0
    for byte in alias.encode("ascii"):
        checksum = (((checksum & 1) << 7) + (checksum >> 1) + byte) & 255
    count = (len(units) + 12) // 13
    units += [0] + [0xFFFF] * 12
    result = []
    for index in range(count, 0, -1):
        entry = bytearray(32)
        entry[0] = index | (0x40 if index == count else 0)
        entry[11] = 0x0F
        entry[13] = checksum
        for offset, value in zip((1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30),
                                 units[(index - 1) * 13:index * 13]):
            put16(entry, offset, value)
        result.append(bytes(entry))
    return result


class Volume:
    def __init__(self, sector_size=512, missing_docs=False, document=None, deep=True):
        self.ss = sector_size
        self.total = 131072 if sector_size == 512 else 66592
        self.reserved = 32
        self.fat_size = (self.total * 4 + sector_size - 1) // sector_size
        self.data_start = self.reserved + self.fat_size * 2
        self.clusters = self.total - self.data_start
        assert self.clusters > 65525
        self.fat = {0: 0x0FFFFFF8, 1: EOC}
        self.sectors = {}
        self.next_cluster = 2000
        self.manifests = {}
        self.root_chain = [5 + i * 37 for i in range(100)]
        self.make_tree(missing_docs, document, deep)

    def allocate(self):
        while self.next_cluster in self.fat or self.next_cluster in self.root_chain:
            self.next_cluster += 1
        cluster = self.next_cluster
        self.next_cluster += 1
        self.fat[cluster] = EOC
        return cluster

    def cluster_sector(self, cluster):
        return self.data_start + cluster - 2

    def directory(self, entries, cluster=None, parent=0, root=False):
        cluster = self.allocate() if cluster is None else cluster
        raw = [] if root else [short_entry(".          ", 0x10, cluster),
                               short_entry("..         ", 0x10, parent)]
        if root:
            raw.append(short_entry("NUSB TEST  ", 0x08))
        for item in entries:
            if root and item["name"] == LONG_NAME:
                # Begin the LFN in the last slot of cluster 5, finish in cluster 42.
                while len(raw) < self.ss // 32 - 1:
                    raw.append(bytes([0xE5]) + bytes(31))
            if item.get("lfn"):
                raw.extend(long_entries(item["name"], item["alias"]))
            raw.append(short_entry(item["alias"], item["attr"], item["cluster"],
                                   item["size"], item.get("lowercase", False)))
        raw.append(bytes(32))
        data = b"".join(raw)
        count = (len(data) + self.ss - 1) // self.ss
        chain = self.root_chain[:count] if root else [cluster] + [self.allocate() for _ in range(count - 1)]
        for index, current in enumerate(chain):
            self.fat[current] = chain[index + 1] if index + 1 < count else EOC
            self.sectors[self.cluster_sector(current)] = data[index * self.ss:(index + 1) * self.ss].ljust(self.ss, b"\0")
        return cluster

    def file(self, name, alias, data=b"", attr=0x20, lfn=False, lowercase=False):
        chain = [self.allocate() for _ in range((len(data) + self.ss - 1) // self.ss)]
        cluster = chain[0] if chain else 0
        for index, current in enumerate(chain):
            self.fat[current] = chain[index + 1] if index + 1 < len(chain) else EOC
            self.sectors[self.cluster_sector(current)] = data[index * self.ss:(index + 1) * self.ss].ljust(self.ss, b"\0")
        return dict(name=name, alias=alias, attr=attr, cluster=cluster, size=len(data),
                    lfn=lfn, lowercase=lowercase)

    def folder(self, name, alias, entries, parent=5, lfn=False, cluster=None):
        cluster = self.directory(entries, cluster, parent)
        return dict(name=name, alias=alias, attr=0x10, cluster=cluster, size=0,
                    lfn=lfn, lowercase=not lfn)

    def make_tree(self, missing_docs, document, deep=True):
        docs = [self.file("nested.txt", "NESTED  TXT", b"abc", lowercase=True)]
        chinese = [self.file("inside.txt", "INSIDE  TXT", b"inside\n", lowercase=True)]
        root = [self.file("README.TXT", "README  TXT", b"Hello nUSB!\n")]
        if not missing_docs:
            root.append(self.folder("docs", "DOCS       ", docs))
        root.extend([self.folder("empty", "EMPTY      ", []),
                     self.file(LONG_NAME, "LONGFI~1TXT", b"long\n", lfn=True),
                     self.folder("资料", "DATA~1     ", chinese, lfn=True),
                     self.file("hidden.txt", "HIDDEN  TXT", b"hidden", attr=0x22, lowercase=True),
                     self.file("system.txt", "SYSTEM  TXT", b"system", attr=0x24, lowercase=True)])
        for index in range(150):
            root.append(self.file("f%03d.txt" % index, "F%03d    TXT" % index,
                                  bytes([index]) * (9000 if index == 149 else index + 1), lowercase=True))
        root.append(self.file(MAX_NAME, "UNICOD~1TXT", b"max", lfn=True))
        if deep:
            # Sixteen long components fit; the next one exceeds the browser path.
            chain = [self.allocate() for _ in range(20)]
            for index in range(len(chain) - 1, -1, -1):
                entries = [] if index == len(chain) - 1 else [
                    dict(name=DEEP_NAME, alias="DEEP~1     ", attr=0x10,
                         cluster=chain[index + 1], size=0, lfn=True)]
                self.directory(entries, chain[index], chain[index - 1] if index else 5)
            root.append(dict(name="deep", alias="DEEP       ", attr=0x10,
                             cluster=chain[0], size=0, lowercase=True))
        if document is not None:
            root.append(self.file("USB Stopwatch.tns", "USBSTO~1TNS", document, lfn=True))
        self.directory(root, 5, root=True)
        self.manifests = {"root": root, "docs": docs, "empty": [], "chinese": chinese}

    def boot(self, start):
        boot = bytearray(self.ss)
        boot[:11] = b"\xEB\x58\x90NUSBTEST"
        put16(boot, 11, self.ss)
        boot[13] = 1
        put16(boot, 14, self.reserved)
        boot[16] = 2
        boot[21] = 0xF8
        put16(boot, 24, 63)
        put16(boot, 26, 255)
        put32(boot, 28, start)
        put32(boot, 32, self.total)
        put32(boot, 36, self.fat_size)
        put32(boot, 44, 5)
        put16(boot, 48, 1)
        put16(boot, 50, 6)
        boot[64] = 0x80
        boot[66] = 0x29
        put32(boot, 67, 0x4E555342)
        boot[71:82] = b"NUSB TEST  "
        boot[82:90] = b"FAT32   "
        boot[510:512] = b"\x55\xAA"
        return boot

    def write(self, path, start=0, second=False, fault=None):
        size = start + self.total
        boot = self.boot(start)
        if fault == "bad-signature":
            boot[510:512] = b"\0\0"
        if fault == "bad-root":
            put32(boot, 44, self.clusters + 9)
        fsinfo = bytearray(self.ss)
        put32(fsinfo, 0, 0x41615252)
        put32(fsinfo, 484, 0x61417272)
        put32(fsinfo, 488, self.clusters - (len(self.fat) - 2))
        put32(fsinfo, 492, self.next_cluster)
        put32(fsinfo, 508, 0xAA550000)
        fat = bytearray(self.fat_size * self.ss)
        for cluster, value in self.fat.items():
            put32(fat, cluster * 4, value)
        with path.open("wb") as image:
            image.truncate(size * self.ss)

            def sector(lba, data):
                image.seek(lba * self.ss)
                image.write(data)

            if start:
                mbr = bytearray(self.ss)
                if second:
                    mbr[446 + 4] = 0x83
                    put32(mbr, 446 + 8, 1)
                    put32(mbr, 446 + 12, start - 1)
                    if fault == "invalid-first":
                        mbr[446 + 4] = 0x0C
                offset = 462 if second else 446
                mbr[offset + 4] = 0x0C
                put32(mbr, offset + 8, start)
                put32(mbr, offset + 12, self.total)
                if fault == "bad-range":
                    put32(mbr, offset + 12, self.total + 1)
                if fault == "overflow-range":
                    put32(mbr, offset + 8, 0xFFFFFFF0)
                    put32(mbr, offset + 12, 0x1000)
                if fault == "short-partition":
                    put32(mbr, offset + 12, 32)
                if fault == "gpt":
                    mbr[offset + 4] = 0xEE
                if fault == "extended":
                    mbr[offset + 4] = 0x0F
                mbr[510:512] = b"\x55\xAA"
                sector(0, mbr)
            sector(start, boot)
            sector(start + 6, boot)
            sector(start + 1, fsinfo)
            sector(start + 7, fsinfo)
            sector(start + self.reserved, fat)
            sector(start + self.reserved + self.fat_size, fat)
            for lba, data in sorted(self.sectors.items()):
                sector(start + lba, data)
            if fault == "truncated":
                image.truncate((start + self.data_start + 4) * self.ss)


def fat16(path):
    boot = bytearray(512)
    boot[:11] = b"\xEB\x3C\x90NUSBTEST"
    put16(boot, 11, 512)
    boot[13] = 1
    put16(boot, 14, 1)
    boot[16] = 2
    put16(boot, 17, 512)
    put16(boot, 19, 32768)
    boot[21] = 0xF8
    put16(boot, 22, 128)
    boot[38] = 0x29
    boot[54:62] = b"FAT16   "
    boot[510:512] = b"\x55\xAA"
    with path.open("wb") as image:
        image.truncate(32768 * 512)
        image.write(boot)
        for lba in (1, 129):
            image.seek(lba * 512)
            image.write(b"\xF8\xFF\xFF\xFF")


def rotate_checksum(data, bits=32, skip=()):
    value = 0
    for index, byte in enumerate(data):
        if index not in skip:
            value = (((value & 1) << (bits - 1)) + (value >> 1) + byte) & ((1 << bits) - 1)
    return value


class ExfatVolume(Volume):
    def __init__(self, sector_size=512, document=None, deep=True):
        self.ss = sector_size
        self.total = 131072
        self.reserved = 24
        self.fat_size = (self.total * 4 + sector_size - 1) // sector_size
        self.data_start = self.reserved + self.fat_size
        self.clusters = self.total - self.data_start
        self.fat = {0: 0xFFFFFFF8, 1: 0xFFFFFFFF}
        self.sectors = {}
        self.next_cluster = 6000
        self.manifests = {}
        self.directory_sizes = {}
        self.root_chain = [5 + i * 37 for i in range(100)]
        self.upper = []
        for value in range(65536):
            upper = chr(value).upper()
            self.upper.append(ord(upper) if len(upper) == 1 and ord(upper) <= 65535 else value)
        encoded = []
        value = 0
        while value < 65536:
            if self.upper[value] != value:
                encoded.append(self.upper[value])
                value += 1
            else:
                end = value + 1
                while end < 65536 and end - value < 65535 and self.upper[end] == end:
                    end += 1
                encoded.extend((0xFFFF, end - value))
                value = end
        self.upcase_data = struct.pack("<%dH" % len(encoded), *encoded)
        self.upcase = self.file("", "", self.upcase_data)
        self.bitmap = self.file("", "", bytes((self.clusters + 7) // 8))
        self.make_tree(False, document, deep)

    def entry_set(self, item):
        name = item["name"].encode("utf-16le")
        units = struct.unpack("<%dH" % (len(name) // 2), name)
        primary = bytearray(32)
        primary[0] = 0x85
        primary[1] = 1 + (len(units) + 14) // 15
        put16(primary, 4, item["attr"])
        for offset in (8, 12, 16):
            put32(primary, offset, 0x5D21645C)
        stream = bytearray(32)
        stream[0] = 0xC0
        stream[1] = 1 if item["attr"] & 0x10 or not item["size"] else 3
        stream[3] = len(units)
        upper = struct.pack("<%dH" % len(units), *(self.upper[u] for u in units))
        put16(stream, 4, rotate_checksum(upper, 16))
        length = self.directory_sizes[item["cluster"]] if item["attr"] & 0x10 else item["size"]
        struct.pack_into("<Q", stream, 8, length)
        put32(stream, 20, item["cluster"])
        struct.pack_into("<Q", stream, 24, length)
        entries = [primary, stream]
        for index in range(0, len(name), 30):
            secondary = bytearray(32)
            secondary[0] = 0xC1
            secondary[2:32] = name[index:index + 30].ljust(30, b"\0")
            entries.append(secondary)
        raw = bytearray(b"".join(entries))
        put16(raw, 2, rotate_checksum(raw, 16, (2, 3)))
        return bytes(raw)

    def directory(self, entries, cluster=None, parent=0, root=False):
        cluster = self.allocate() if cluster is None else cluster
        raw = bytearray()
        if root:
            bitmap = bytearray(32)
            bitmap[0] = 0x81
            put32(bitmap, 20, self.bitmap["cluster"])
            struct.pack_into("<Q", bitmap, 24, self.bitmap["size"])
            upcase = bytearray(32)
            upcase[0] = 0x82
            put32(upcase, 4, rotate_checksum(self.upcase_data))
            put32(upcase, 20, self.upcase["cluster"])
            struct.pack_into("<Q", upcase, 24, self.upcase["size"])
            raw.extend(bitmap + upcase)
        for item in entries:
            raw.extend(self.entry_set(item))
        raw.extend(bytes(32))
        count = (len(raw) + self.ss - 1) // self.ss
        chain = self.root_chain[:count] if root else [cluster] + [self.allocate() for _ in range(count - 1)]
        self.directory_sizes[cluster] = count * self.ss
        for index, current in enumerate(chain):
            self.fat[current] = chain[index + 1] if index + 1 < count else 0xFFFFFFFF
            self.sectors[self.cluster_sector(current)] = raw[index * self.ss:(index + 1) * self.ss].ljust(self.ss, b"\0")
        return cluster

    def boot(self, start):
        boot = bytearray(12 * self.ss)
        boot[:11] = b"\xEB\x76\x90EXFAT   "
        struct.pack_into("<QQ", boot, 64, start, self.total)
        for offset, value in ((80, self.reserved), (84, self.fat_size),
                              (88, self.data_start), (92, self.clusters), (96, 5),
                              (100, 0x4E555342)):
            put32(boot, offset, value)
        put16(boot, 104, 0x100)
        boot[108] = self.ss.bit_length() - 1
        boot[109] = 0
        boot[110] = 1
        boot[111] = 0x80
        boot[112] = 0xFF
        boot[510:512] = b"\x55\xAA"
        for index in range(1, 9):
            put32(boot, (index + 1) * self.ss - 4, 0xAA550000)
        checksum = rotate_checksum(boot[:11 * self.ss], skip=(106, 107, 112))
        boot[11 * self.ss:] = struct.pack("<I", checksum) * (self.ss // 4)
        return boot

    def write(self, path, start=0, fault=None):
        boot = self.boot(start)
        if fault in ("dirty", "media-failure"):
            put16(boot, 106, 2 if fault == "dirty" else 4)
        geometry = {
            "bad-geometry": (88, self.total), "bad-fat": (84, 1),
            "bad-heap": (88, self.reserved), "bad-root": (96, self.clusters + 2),
            "bad-clusters": (92, 0xFFFFFFFE),
        }
        if fault in geometry:
            offset, value = geometry[fault]
            put32(boot, offset, value)
        if fault == "bad-volume":
            struct.pack_into("<Q", boot, 72, self.total + 1)
        if fault == "bad-shift":
            boot[109] = 255
        if fault in geometry or fault in ("bad-volume", "bad-shift"):
            checksum = rotate_checksum(boot[:11 * self.ss], skip=(106, 107, 112))
            boot[11 * self.ss:] = struct.pack("<I", checksum) * (self.ss // 4)
        if fault == "bad-checksum":
            boot[11 * self.ss] ^= 1
        bitmap = bytearray(self.bitmap["size"])
        for cluster in self.fat:
            if cluster >= 2:
                bitmap[(cluster - 2) // 8] |= 1 << ((cluster - 2) % 8)
        current = self.bitmap["cluster"]
        for offset in range(0, len(bitmap), self.ss):
            self.sectors[self.cluster_sector(current)] = bitmap[offset:offset + self.ss].ljust(self.ss, b"\0")
            current = self.fat[current]
        fat = bytearray(self.fat_size * self.ss)
        for cluster, value in self.fat.items():
            put32(fat, cluster * 4, 0xFFFFFFFF if value == EOC else value)
        with path.open("wb") as image:
            image.truncate((start + self.total) * self.ss)
            if start:
                mbr = bytearray(self.ss)
                mbr[450] = 7
                put32(mbr, 454, start)
                put32(mbr, 458, self.total)
                mbr[510:512] = b"\x55\xAA"
                image.write(mbr)
            for lba, data in [(start, boot), (start + 12, boot),
                              (start + self.reserved, fat)]:
                image.seek(lba * self.ss)
                image.write(data)
            for lba, data in sorted(self.sectors.items()):
                image.seek((start + lba) * self.ss)
                image.write(data)
            if fault and fault.startswith("bitmap-"):
                entry = bytearray(32)
                entry[0] = 0x81
                put32(entry, 20, self.bitmap["cluster"])
                struct.pack_into("<Q", entry, 24, self.bitmap["size"])
                if fault == "bitmap-missing":
                    entry[0] = 0x01
                elif fault == "bitmap-id":
                    entry[1] = 1
                elif fault == "bitmap-bounds":
                    put32(entry, 20, self.clusters + 2)
                elif fault == "bitmap-short":
                    struct.pack_into("<Q", entry, 24, 1)
                elif fault in ("bitmap-loop", "bitmap-fragmented"):
                    image.seek((start + self.reserved) * self.ss + self.bitmap["cluster"] * 4)
                    image.write(struct.pack("<I", self.bitmap["cluster"] +
                                            (2 if fault == "bitmap-fragmented" else 0)))
                image.seek((start + self.cluster_sector(5)) * self.ss)
                image.write(entry)
            if fault in ("bad-vdl", "short-vdl", "bad-set-checksum"):
                offset = (start + self.cluster_sector(5)) * self.ss + 64
                raw = bytearray(self.entry_set(self.manifests["root"][0]))
                if fault == "bad-set-checksum":
                    raw[2] ^= 1
                else:
                    struct.pack_into("<Q", raw, 40, 13 if fault == "bad-vdl" else 5)
                    put16(raw, 2, rotate_checksum(raw, 16, (2, 3)))
                image.seek(offset)
                image.write(raw)


LARGE_SIZE = (1 << 32) + 12345
LARGE_SENTINELS = (0, 65504, (1 << 31) - 32, (1 << 32) - 32,
                   (1 << 32) + 32, LARGE_SIZE - 64)


def populate_large(path, fragmented):
    """Add a sparse source to an independently mkfs-formatted 12 GiB volume."""
    with path.open("r+b") as image:
        boot = image.read(512)
        ss = 1 << boot[108]
        cluster_bytes = ss << boot[109]
        fat_start, fat_size, heap, clusters, root = struct.unpack_from("<5I", boot, 80)
        if boot[3:11] != b"EXFAT   " or ss != 512 or cluster_bytes != 65536:
            raise ValueError("large fixture needs exFAT, 512-byte sectors, 64 KiB clusters")

        def position(cluster):
            return heap * ss + (cluster - 2) * cluster_bytes

        image.seek(position(root))
        directory = bytearray(image.read(cluster_bytes))
        bitmap_entry = next(directory[i:i + 32] for i in range(0, len(directory), 32)
                            if directory[i] == 0x81 and directory[i + 1] == 0)
        bitmap_cluster = struct.unpack_from("<I", bitmap_entry, 20)[0]
        bitmap_size = struct.unpack_from("<Q", bitmap_entry, 24)[0]
        image.seek(position(bitmap_cluster))
        bitmap = bytearray(image.read(bitmap_size))
        free = [c for c in range(2, clusters + 2)
                if not (bitmap[(c - 2) // 8] >> ((c - 2) % 8)) & 1]
        count = (LARGE_SIZE + cluster_bytes - 1) // cluster_bytes
        chain = free[::2][:count] if fragmented else free[:count]
        if len(chain) != count or (not fragmented and chain[-1] - chain[0] + 1 != count):
            raise ValueError("not enough suitable free clusters")
        for index, cluster in enumerate(chain):
            bitmap[(cluster - 2) // 8] |= 1 << ((cluster - 2) % 8)
            if fragmented:
                image.seek(fat_start * ss + cluster * 4)
                image.write(struct.pack("<I", chain[index + 1] if index + 1 < count else 0xFFFFFFFF))
        image.seek(position(bitmap_cluster))
        image.write(bitmap)
        volume = ExfatVolume.__new__(ExfatVolume)
        volume.upper = list(range(65536))
        for value in range(ord("a"), ord("z") + 1):
            volume.upper[value] = value - 32
        raw = bytearray(volume.entry_set(dict(name="source.bin", attr=0x20,
                                              cluster=chain[0], size=LARGE_SIZE)))
        raw[33] = 1 if fragmented else 3
        put16(raw, 2, rotate_checksum(raw, 16, (2, 3)))
        end = next(i for i in range(0, len(directory), 32) if directory[i] == 0)
        if end + len(raw) + 32 > len(directory):
            raise ValueError("root directory full")
        directory[end:end + len(raw)] = raw
        image.seek(position(root))
        image.write(directory)
        for offset in LARGE_SENTINELS:
            data = bytes((offset + i) % 251 + 1 for i in range(64))
            while data:
                index, within = divmod(offset, cluster_bytes)
                size = min(len(data), cluster_bytes - within)
                image.seek(position(chain[index]) + within)
                image.write(data[:size])
                offset += size
                data = data[size:]
        for offset in (112, 12 * ss + 112):
            image.seek(offset)
            image.write(b"\xFF")


def large_images(output, mkfs):
    for fragmented in (False, True):
        path = output / ("large-fragmented.img" if fragmented else "large-contiguous.img")
        with path.open("xb") as image:
            image.truncate(12 << 30)
        subprocess.run([str(mkfs), "-c", "65536", str(path)], check=True)
        populate_large(path, fragmented)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", nargs="?", type=Path, default=Path("build/fixtures"))
    parser.add_argument("--document", type=Path, help="add a real TNS document as USB Stopwatch.tns")
    parser.add_argument("--large", action="store_true", help="create only the two sparse 12 GiB fixtures")
    parser.add_argument("--mkfs-exfat", type=Path, help="explicit mkfs.exfat executable for --large")
    args = parser.parse_args()
    document = args.document.read_bytes() if args.document else None
    args.output.mkdir(parents=True, exist_ok=True)
    if args.large:
        if args.mkfs_exfat is None or not args.mkfs_exfat.is_file():
            parser.error("--large requires an explicit --mkfs-exfat executable")
        large_images(args.output, args.mkfs_exfat.resolve())
        return
    volume = Volume(document=document)
    volume.write(args.output / "fat32-sfd.img")
    volume.write(args.output / "fat32-mbr.img", start=2048, second=True)
    volume.write(args.output / "fat32-mbr-first.img", start=2048)
    volume.write(args.output / "fat32-mbr-skip.img", start=2048, second=True, fault="invalid-first")
    Volume(document=document, deep=False).write(args.output / "fat32-namespace.img")
    cyclic = Volume(document=document, deep=False)
    docs = cyclic.manifests["root"][1]
    cyclic.directory([dict(name="loop", alias="LOOP       ", attr=0x10,
                           cluster=5, size=0)], docs["cluster"], parent=5)
    cyclic.write(args.output / "fat32-namespace-cycle.img")
    duplicate = Volume(document=document, deep=False)
    root = duplicate.manifests["root"]
    root.append(dict(root[1], name="duplicate", alias="DUPLICAT   "))
    duplicate.directory(root, 5, root=True)
    duplicate.write(args.output / "fat32-namespace-duplicate.img")
    for size in (1024, 2048, 4096):
        Volume(size, document=document).write(args.output / ("fat32-%d.img" % size))
    for fault in ("bad-signature", "bad-root", "truncated"):
        volume.write(args.output / (fault + ".img"), fault=fault)
    for fault in ("bad-range", "overflow-range", "short-partition", "gpt", "extended"):
        volume.write(args.output / (fault + ".img"), start=2048, second=True, fault=fault)
    Volume(missing_docs=True, document=document).write(args.output / "fat32-missing-docs.img")
    broken = Volume(document=document)
    broken.manifests["root"][1]["cluster"] = broken.clusters + 9
    broken.directory(broken.manifests["root"], 5, root=True)
    broken.write(args.output / "bad-directory.img")
    broken = Volume(document=document)
    broken.fat[5] = 1
    broken.write(args.output / "bad-chain.img")
    for name, link in (("out-of-range-chain", broken.clusters + 9),
                       ("reserved-chain", 0x0FFFFFF0),
                       ("bad-cluster-chain", 0x0FFFFFF7),
                       ("eoc-low-chain", 0x0FFFFFF8),
                       ("eoc-high-chain", 0x0FFFFFFF)):
        broken.fat[5] = link
        broken.write(args.output / (name + ".img"))
    fat16(args.output / "fat16.img")
    with (args.output / "exfat.img").open("wb") as image:
        boot = bytearray(512)
        boot[:11] = b"\xEB\x76\x90EXFAT   "
        boot[510:512] = b"\x55\xAA"
        image.write(boot)
        image.truncate(1024 * 512)
    exfat = ExfatVolume(document=document)
    exfat.write(args.output / "exfat-sfd.img")
    exfat.write(args.output / "exfat-mbr.img", start=2048)
    ExfatVolume(document=document, deep=False).write(args.output / "exfat-namespace.img")
    semantics = ExfatVolume(document=document, deep=False)
    nested = [semantics.file("leaf.txt", "", b"deep")]
    for _ in range(40):
        nested = [semantics.folder("d", "", nested)]
    mixed_name = "a" * 125 + "\U00010437" * 63 + ".txt"
    root = semantics.manifests["root"]
    root.extend(nested)
    root.append(semantics.file(mixed_name, "", b"unicode"))
    root.append(semantics.folder("l" * 255, "", [semantics.file("leaf.txt", "", b"long")]))
    semantics.directory(root, 5, root=True)
    semantics.write(args.output / "exfat-semantics.img")
    exfat4096 = ExfatVolume(4096, document=document)
    exfat4096.write(args.output / "exfat-4096.img")
    exfat4096.write(args.output / "exfat-mbr-4096.img", start=256)
    for fault in ("bad-checksum", "bad-geometry", "bad-fat", "bad-heap", "bad-root",
                  "bad-volume", "bad-clusters", "bad-shift", "dirty", "media-failure",
                  "bad-vdl", "short-vdl", "bad-set-checksum", "bitmap-missing",
                  "bitmap-id", "bitmap-bounds", "bitmap-short", "bitmap-loop", "bitmap-fragmented"):
        exfat.write(args.output / ("exfat-" + fault + ".img"), fault=fault)
    for name, entries in volume.manifests.items():
        with (args.output / (name + ".tsv")).open("w", encoding="utf-8", newline="\n") as manifest:
            for item in entries:
                manifest.write("%s\t%d\t%s\n" %
                               ("D" if item["attr"] & 0x10 else "F", item["size"], item["name"]))
    (args.output / "fixture.json").write_text(json.dumps({
        "fatfs_commit": COMMIT, "root_cluster": 5, "root_entries": len(volume.manifests["root"]),
        "deep_component": DEEP_NAME, "max_name": MAX_NAME,
        "root_chain": volume.root_chain,
    }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
