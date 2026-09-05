#!/usr/bin/env python3
"""
uplink_reader.py -- PC-side reader for the PMD:ES DSpico USB autosplitter.

The mod presents itself as a USB CDC-ACM (serial) device and streams 36-byte
little-endian payloads on the bulk IN endpoint. This tool opens the COM port,
re-synchronizes on the magic, validates each packet (magic / version / CRC16 /
sequence gaps) and prints the autosplitter values so you can wire them into a
Livesplit auto-splitter.

Usage:
    python uplink_reader.py --list
    python uplink_reader.py COM5
    python uplink_reader.py /dev/ttyACM0

Requires pyserial:  pip install pyserial
"""

import argparse
import struct
import sys

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("pyserial is required:  pip install pyserial", file=sys.stderr)
    sys.exit(1)

MAGIC = 0x5350          # "PS", stored little-endian -> b'\x50\x53'
VERSION = 1
SIZE = 36


def crc16_ccitt(data: bytes) -> int:
    """CRC16-CCITT-FALSE: poly 0x1021, init 0xFFFF (matches the mod)."""
    crc = 0xFFFF
    for b in data:
        crc ^= (b << 8) & 0xFFFF
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def parse(buf: bytes):
    """Parse a 36-byte packet into a dict, or return None on bad magic/version."""
    magic, = struct.unpack_from('<H', buf, 0)
    if magic != MAGIC:
        return None
    version = buf[2]
    if version != VERSION:
        return None
    d = {
        'flags': buf[3],
        'seq': struct.unpack_from('<H', buf, 4)[0],
        'crc': struct.unpack_from('<H', buf, 6)[0],
        'play_time_seconds': struct.unpack_from('<I', buf, 0x08)[0],
        'play_time_frames': buf[0x0C],
        'v_22ABAA8': buf[0x0D],
        'v_22ABAA9': buf[0x0E],
        'v_22ABADB': buf[0x0F],
        'v_2325ACA': buf[0x10],
        'v_22A40E4': struct.unpack_from('<I', buf, 0x11)[0],
        'main_menu_magic': struct.unpack_from('<I', buf, 0x15)[0],
        'v_2329D40': struct.unpack_from('<I', buf, 0x19)[0],
        'dungeon_end_floor_flag': buf[0x1D],
        'dungeon_id': buf[0x1E],
        'dungeon_floor': buf[0x1F],
    }
    return d


def fmt_time(sec: int, frames: int) -> str:
    total = sec + frames / 60.0
    m, s = divmod(int(total), 60)
    h, m = divmod(m, 60)
    return f"{h:02d}:{m:02d}:{s:02d}.{frames:02d}"


def main() -> int:
    ap = argparse.ArgumentParser(description="PMD:ES DSpico uplink reader")
    ap.add_argument("port", nargs="?", help="COM port (e.g. COM5 /dev/ttyACM0)")
    ap.add_argument("--list", action="store_true", help="list serial ports and exit")
    ap.add_argument("--raw", action="store_true",
                    help="print every field; default prints a compact summary line")
    ap.add_argument("--baud", type=int, default=115200,
                    help="baud rate (ignored for USB CDC, kept for compatibility)")
    args = ap.parse_args()

    if args.list:
        for p in serial.tools.list_ports.comports():
            print(f"{p.device}\t{p.description}")
        return 0

    if not args.port:
        ports = [p.device for p in serial.tools.list_ports.comports()]
        print("No port given. Available ports:", file=sys.stderr)
        for p in ports:
            print(f"  {p}", file=sys.stderr)
        return 1

    try:
        ser = serial.Serial(args.port, args.baud, timeout=1)
    except serial.SerialException as e:
        print(f"Failed to open {args.port}: {e}", file=sys.stderr)
        return 1

    print(f"Reading {args.port} ... (Ctrl-C to stop)", file=sys.stderr)

    buf = bytearray()
    last_seq = None
    good = 0
    crc_bad = 0
    version_bad = 0
    gaps = 0

    try:
        while True:
            data = ser.read(4096)
            if data:
                buf.extend(data)
            # Re-sync on the magic and consume complete packets.
            while len(buf) >= SIZE:
                idx = buf.find(b'\x50\x53')
                if idx < 0:
                    if len(buf) > 1:
                        del buf[:-1]
                    break
                if idx > 0:
                    del buf[:idx]
                if len(buf) < SIZE:
                    break
                pkt = bytes(buf[:SIZE])
                del buf[:SIZE]

                d = parse(pkt)
                if d is None:
                    # magic matched but version differs (or we mis-synced)
                    version_bad += 1
                    continue
                # The mod computes the CRC over the full 36-byte payload with
                # the crc field itself zeroed (pkt[6:8] == b'\x00\x00').
                calc = crc16_ccitt(pkt[:6] + b"\x00\x00" + pkt[8:])
                if calc != d['crc']:
                    crc_bad += 1
                    # resync: drop the first magic byte and re-scan
                    continue
                good += 1

                if last_seq is not None:
                    expected = (last_seq + 1) & 0xFFFF
                    if d['seq'] != expected:
                        gaps += 1
                last_seq = d['seq']

                if args.raw:
                    print(
                        f"[{d['seq']:05d}] t={fmt_time(d['play_time_seconds'], d['play_time_frames'])} "
                        f"flags=0x{d['flags']:02x} "
                        f"22ABAA8={d['v_22ABAA8']:#04x} 22ABAA9={d['v_22ABAA9']:#04x} "
                        f"22ABADB={d['v_22ABADB']:#04x} 2325ACA={d['v_2325ACA']:#04x} "
                        f"22A40E4={d['v_22A40E4']:#010x} 22A3670={d['main_menu_magic']:#010x} "
                        f"2329D40={d['v_2329D40']:#010x} "
                        f"dgn[id={d['dungeon_id']} fl={d['dungeon_floor']} end={d['dungeon_end_floor_flag']}]"
                    )
                else:
                    in_dgn = "D" if (d['flags'] & 0x1) else "."
                    print(
                        f"[{d['seq']:05d}] {fmt_time(d['play_time_seconds'], d['play_time_frames'])} "
                        f"{in_dgn} id={d['dungeon_id']:02d} fl={d['dungeon_floor']:02d} "
                        f"end={d['dungeon_end_floor_flag']}"
                    )
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()
        print(f"\npackets ok={good} crc_bad={crc_bad} version_bad={version_bad} seq_gaps={gaps}",
              file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())