#!/usr/bin/env python3
"""PC-side reader for the PMDSky speedrun-mod uplink.

The mod streams ARM9 memory samples to the PC over a DSpico-backed USB
CDC serial port (VID 0x2020, PID 0xD801). See src/uplink/uplink.c and
src/uplink/uplink_sampler.h for the on-device implementation.

Telemetry stream (device -> host, raw bytes, no line framing):
  54-byte frames, sent in 486-byte blocks (9 frames):
    [0..1]   magic 'P' 'M'
    [2..5]   seq          frame index (u32 LE)
    [6..9]   game_frame   PLAY_TIME as seconds*60 + frames (u32 LE)
    [10..49] samples[10]  u32 LE, one per sample slot
    [50..51] checksum     sum of bytes [0..49] (u16 LE)
    [52..53] padding      zero

Host commands (host -> device, raw bytes):
    0x01            start sampling
    0x02            stop sampling
    0x03 idx a32    retarget sample slot idx to address (0 disables)
    0x04            ping; device replies with
                    "UPLINK seq=<n> sent=<n> drop=<n>\r\n" on the same
                    stream (mixed in between telemetry frames)

Default sample slots (uplink_sampler_init):
    0 PLAY_TIME_SECONDS   1 PLAY_TIME_FRAME_COUNTER   2 start_time
    3 file_timer          4 hud_display_mode          5 REG_MCCNT1
    6 REG_MCCNT0          7 uplink_frames_sent        8 uplink_frames_dropped
    9 uplink_card_lock_skips

Usage examples:
    python tools/pc_reader.py                      # auto-detect, live tail
    python tools/pc_reader.py --port COM5 --csv run1.csv
    python tools/pc_reader.py --ping --duration 5
    python tools/pc_reader.py --set-addr 5 0x022ABFD4
"""
import argparse
import struct
import sys
import time

MAGIC = b'PM'
FRAME_LEN = 54
SAMPLE_COUNT = 10
UPLINK_VID = 0x2020
UPLINK_PID = 0xD801

CMD_START = b'\x01'
CMD_STOP = b'\x02'
CMD_PING = b'\x04'

DEFAULT_SLOT_NAMES = [
    'PLAY_TIME_SECONDS', 'PLAY_TIME_FRAME_COUNTER', 'start_time',
    'file_timer', 'hud_display_mode', 'REG_MCCNT1', 'REG_MCCNT0',
    'uplink_frames_sent', 'uplink_frames_dropped', 'uplink_card_lock_skips',
]


def import_serial():
    try:
        import serial
        return serial
    except ImportError:
        sys.exit('pyserial is required: pip install pyserial')


def find_uplink_port(serial_mod):
    from serial.tools import list_ports
    for port in list_ports.comports():
        if port.vid == UPLINK_VID and port.pid == UPLINK_PID:
            return port.device
    return None


def parse_args(argv):
    p = argparse.ArgumentParser(description='PMDSky uplink CDC telemetry reader')
    p.add_argument('--port', help='serial port (e.g. COM5 or /dev/ttyACM0); '
                                  'default: auto-detect VID %04x/PID %04x'
                                  % (UPLINK_VID, UPLINK_PID))
    p.add_argument('--baud', type=int, default=115200,
                   help='baud rate (ignored by CDC; default 115200)')
    p.add_argument('--csv', metavar='FILE', help='write telemetry to a CSV file')
    p.add_argument('--list', action='store_true', help='list candidate ports and exit')
    p.add_argument('--start', action='store_true', help='send START (0x01) after opening')
    p.add_argument('--stop', action='store_true', help='send STOP (0x02) after opening')
    p.add_argument('--ping', action='store_true', help='send PING (0x04) after opening')
    p.add_argument('--set-addr', nargs=2, metavar=('SLOT', 'ADDR'), action='append',
                   help='retarget sample slot (0-9) to ADDR (hex); repeatable')
    p.add_argument('--duration', type=float, metavar='SECONDS',
                   help='stop reading after this many seconds (default: Ctrl+C)')
    p.add_argument('-v', '--verbose', action='store_true', help='print every frame')
    p.add_argument('-q', '--quiet', action='store_true', help='print no per-frame output')
    return p.parse_args(argv)


def fmt_game_frame(gf):
    # gf = seconds*60 + frames
    total_seconds = gf // 60
    frames = gf % 60
    return '%d:%02d:%02d' % (total_seconds // 60, total_seconds % 60, frames)


class Stats:
    def __init__(self):
        self.frames = 0
        self.bad_checksum = 0
        self.seq_gaps = 0
        self.gap_frames = 0
        self.text_lines = 0
        self.last_seq = None
        self.bytes = 0
        self.start_time = time.time()

    def note_seq(self, seq):
        if self.last_seq is not None and seq > self.last_seq + 1:
            self.seq_gaps += 1
            self.gap_frames += seq - self.last_seq - 1
        self.last_seq = seq

    def report(self):
        dt = max(time.time() - self.start_time, 1e-9)
        print('\n--- summary ---')
        print('frames received : %d' % self.frames)
        print('bytes received  : %d (%.1f KB/s)' % (self.bytes, self.bytes / 1024 / dt))
        print('checksum errors : %d' % self.bad_checksum)
        print('seq gaps        : %d (%d frames lost, last seq %s)'
              % (self.seq_gaps, self.gap_frames,
                 'n/a' if self.last_seq is None else str(self.last_seq)))
        print('host text lines : %d' % self.text_lines)


def process_frame(frame, stats, csv_file, args):
    seq, gf = struct.unpack_from('<II', frame, 2)
    samples = struct.unpack_from('<10I', frame, 10)
    stats.frames += 1
    stats.note_seq(seq)

    if csv_file is not None:
        csv_file.write('%d,%d,,%d,' % (int(time.time() * 1000), seq, gf))
        csv_file.write(','.join(str(s) for s in samples))
        csv_file.write('\n')

    if args.verbose:
        s = ' '.join('%d=0x%08x' % (i, samples[i]) for i in range(SAMPLE_COUNT))
        print('seq=%-9d %s  %s' % (seq, fmt_game_frame(gf), s))
    elif not args.quiet and stats.frames % 300 == 1:
        # periodic one-liner: every 300 frames (~5 s at 60 Hz)
        print('.. seq=%d game=%d:%02d:%02d sent=%d drop=%d'
              % (seq, gf // 3600, (gf // 60) % 60, gf % 60,
                 samples[7], samples[8]))


def handle_stray(data, stats):
    # Non-frame bytes: usually the PING reply line. Show it, ignore the rest.
    for line in data.split(b'\n'):
        line = line.strip()
        if line.startswith(b'PUPLINK'):
            line = line[1:]  # resync 'P' merged into the ping line
        if line:
            stats.text_lines += 1
            print('host> ' + line.decode('ascii', 'replace'))


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
    serial = import_serial()

    if args.list:
        from serial.tools import list_ports
        for port in list_ports.comports():
            mark = '  <-- uplink' if (port.vid == UPLINK_VID and port.pid == UPLINK_PID) else ''
            print('%-8s %04x:%04x %s%s' % (port.device, port.vid or 0, port.pid or 0,
                                           (port.product or ''), mark))
        return 0

    port = args.port or find_uplink_port(serial)
    if not port:
        sys.exit('No serial port given and no device with VID %04x/PID %04x '
                 'found. Use --port. (The NDS must be powered on with the '
                 'DSpico plugged into a powered USB port.)' % (UPLINK_VID, UPLINK_PID))

    csv_file = None
    if args.csv:
        csv_file = open(args.csv, 'w', newline='')
        csv_file.write('recv_ms,seq,gap,game_frame,' + ','.join(DEFAULT_SLOT_NAMES) + '\n')

    try:
        ser = serial.Serial(port, args.baud, timeout=0.5)
    except serial.SerialException as e:
        sys.exit('Cannot open %s: %s' % (port, e))

    print('Uplink reader on %s (VID %04x PID %04x)' % (port, UPLINK_VID, UPLINK_PID))

    # Host commands first, so sampling state is set before we start reading.
    if args.start:
        ser.write(CMD_START)
        print('sent START')
    if args.stop:
        ser.write(CMD_STOP)
        print('sent STOP')
    for slot, addr in (args.set_addr or []):
        try:
            idx = int(slot)
            value = int(addr, 0)
        except ValueError:
            sys.exit('--set-addr takes SLOT (0-9) and ADDR (hex)')
        if not 0 <= idx <= SAMPLE_COUNT - 1:
            sys.exit('sample slot out of range: %d' % idx)
        ser.write(bytes([0x03, idx]) + struct.pack('<I', value))
        print('sent SET_ADDR slot=%d addr=0x%08x' % (idx, value & 0xFFFFFFFF))
    if args.ping:
        ser.write(CMD_PING)
        print('sent PING')

    stats = Stats()
    buf = bytearray()
    stray = bytearray()
    deadline = None if not args.duration else time.time() + args.duration
    try:
        while deadline is None or time.time() < deadline:
            data = ser.read(512)
            if not data:
                continue
            stats.bytes += len(data)
            buf += data
            while True:
                idx = buf.find(MAGIC)
                if idx < 0:
                    # Keep a trailing 'P' (possible split magic); the rest is
                    # stray data (e.g. a ping reply).
                    if buf:
                        if buf[-1:] == b'P':
                            stray += bytes(buf[:-1])
                            del buf[:-1]
                        else:
                            stray += bytes(buf)
                            del buf[:]
                    if stray and (stray.endswith(b'\n') or len(stray) > 4096):
                        handle_stray(bytes(stray), stats)
                        del stray[:]
                    break
                if idx > 0:
                    stray += bytes(buf[:idx])
                    del buf[:idx]
                    if stray.endswith(b'\n') or len(stray) > 4096:
                        handle_stray(bytes(stray), stats)
                        del stray[:]
                if len(buf) < FRAME_LEN:
                    break
                frame = bytes(buf[:FRAME_LEN])
                del buf[:FRAME_LEN]
                stored, = struct.unpack_from('<H', frame, 50)
                if sum(frame[:50]) & 0xFFFF != stored:
                    stats.bad_checksum += 1
                    buf.insert(0, 0x50)  # 'P': resync one byte later
                    continue
                process_frame(frame, stats, csv_file, args)
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()
        if csv_file:
            csv_file.close()
            print('wrote %s' % args.csv)
        stats.report()
    return 0


if __name__ == '__main__':
    sys.exit(main())

