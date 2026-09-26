namespace UplinkReader;

/// <summary>
/// One decoded 54-byte uplink frame. The named properties mirror the
/// mod's default sample slots (uplink_sampler_init in
/// src/uplink/uplink_sampler.c); RawValues keeps the raw slot array in
/// case slots are retargeted on-device.
/// </summary>
public sealed class UplinkSample
{
    public required uint Seq { get; init; }

    /// <summary>PLAY_TIME at sample time: seconds*60 + frames.</summary>
    public required uint GameFrame { get; init; }

    public DateTime ReceivedUtc { get; init; } = DateTime.UtcNow;

    public required uint[] RawValues { get; init; }

    /// <summary>Default slot names, in slot order (0-9).</summary>
    public static readonly string[] SlotNames =
    {
        "PLAY_TIME_SECONDS", "PLAY_TIME_FRAME_COUNTER", "start_time",
        "file_timer", "hud_display_mode", "REG_MCCNT1", "REG_MCCNT0",
        "uplink_frames_sent", "uplink_frames_dropped", "uplink_card_lock_skips",
    };

    public uint PlayTimeSeconds => RawValues[0];
    public uint PlayTimeFrameCounter => RawValues[1];
    public uint StartTime => RawValues[2];
    public uint FileTimer => RawValues[3];
    public uint HudDisplayMode => RawValues[4];
    public uint RegMccnt1 => RawValues[5];
    public uint RegMccnt0 => RawValues[6];
    public uint UplinkFramesSent => RawValues[7];
    public uint UplinkFramesDropped => RawValues[8];
    public uint UplinkCardLockSkips => RawValues[9];

    public uint this[int slot] => RawValues[slot];

    /// <summary>GameFrame as h:mm:ss (seconds*60 + frames, like the mod's PLAY_TIME).</summary>
    public string FormatGameTime()
    {
        long totalSeconds = GameFrame / 60;
        int frames = (int)(GameFrame % 60);
        return $"{totalSeconds / 60}:{totalSeconds % 60:00}:{frames:00}";
    }
}
