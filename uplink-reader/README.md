# PMDSky Uplink Reader (C#)

C#/.NET 8 reader for the PMDSky speedrun mod's DSPico uplink link
(CDC-ACM serial, VID `0x2020` / PID `0xD801`, 115200 8N1). This is a
rewrite of `tools/pc_reader.py` — same wire protocol (v1), same
behavior (START on every (re)connect, one-shot PING on first connect,
persistent reconnect, 3 s silence watchdog) — with an Avalonia GUI and
a debug console log. No firmware, USB transport, or Python code is
touched.

## Wire protocol (v1, unchanged)

54-byte frames: `PM` magic, 32-bit LE seq, 32-bit LE game frame
(seconds*60 + frames), 10 x 32-bit LE sample slots, 16-bit LE checksum
over bytes 0-49, 2 bytes padding. Host commands: `0x01` START, `0x02`
STOP, `0x03 idx addr` SET_ADDR, `0x04` PING (device replies with
`UPLINK seq=N sent=N drop=N` on the same stream).

Default slots: `PLAY_TIME_SECONDS`, `PLAY_TIME_FRAME_COUNTER`,
`start_time`, `file_timer`, `hud_display_mode`, `REG_MCCNT1`,
`REG_MCCNT0`, `uplink_frames_sent`, `uplink_frames_dropped`,
`uplink_card_lock_skips`.

## Build

```
dotnet build uplink-reader/uplink-reader.csproj -c Release
```

(Requires the .NET 8 SDK and NuGet access for Avalonia on first build.)

## Run

The app takes no launch parameters — it always starts the GUI:

```
dotnet uplink-reader/bin/Release/net8.0/uplink-reader.dll
```

(`dotnet run --project uplink-reader` works too.)

- **Release** builds are a plain GUI app (no console window, no logging).
- **Debug** builds are a console app: in addition to the GUI, every
  frame (`seq=... <game time>  slot=value ...` at ~60 Hz), session
  events (connect / reconnect / watchdog), device lines (`host> ...`),
  and a session summary on exit are logged to standard output. This
  replaces the old `--console` mode; it is compiled in with `#if DEBUG`
  and is not toggled by a launch parameter.

The port list in the GUI auto-refreshes every 3 s (new ports, VID/PID
changes, unplug/replug); the Refresh button remains for an immediate
refresh.

## Notes

- Config (auto-connect + last port) is stored in the platform app-data
  location (`%APPDATA%\PmdskyUplinkReader\config.json` on Windows,
  `~/.config/pmdsky-uplink-reader/config.json` on Linux,
  `~/Library/Application Support/PmdskyUplinkReader/config.json` on
  macOS).
- VID/PID port detection works on Windows (registry) and Linux (/sys);
  on macOS pick the port manually from the dropdown.
- Samples are exposed through `UplinkClient.Latest` (a `UplinkSample`
  with named fields + raw values) and the `FrameReceived` event for
  further use; the GUI itself does not display values.
- Soft resets (and unplug/replug) are handled automatically: the link
  is dropped and reconnected with a fresh START, exactly like
  `tools/pc_reader.py`.
- The offline protocol self-test (`SelfTestRunner` in
  `SelfTest/SelfTest.cs`) is no longer reachable from the app (it used
  `--selftest`); it remains in the tree for a future unit-test project.
