using System.Buffers.Binary;

namespace UplinkReader.Protocol;

/// <summary>
/// Publishes the most recent validated uplink frame into a single static
/// byte block so an external memory scanner can locate it by byte pattern
/// and read the latest frame at a fixed offset.
///
/// The block is a static readonly array - a GC root in the non-compacting
/// .NET heap - so its address is stable for the lifetime of the process.
/// It differs between app launches, so locate it by scanning for the
/// magic at runtime; never cache the address across runs.
///
/// Layout (all multi-byte values little-endian):
///
///   offset  size  content
///   0       8     magic 50 4D 55 50 4C 49 4E 4B ("PMUPLINK")
///   8      54     raw wire frame - the exact 54 bytes the device sent
///                 (PM magic, seq, game_frame, samples[10], checksum, pad)
///   62      4     seqlock u32 - odd while a frame copy is in progress,
///                 even when the frame bytes are a stable snapshot
///
/// Scanner recipe: find the 8 magic bytes, read the frame at +8. For a
/// strict consistent snapshot, read the seqlock u32 at +62 before and
/// after the frame read and require it to be even and unchanged. As a
/// lighter check, the frame's own checksum (sum of frame bytes 0-49
/// &amp; 0xFFFF == frame bytes 50-51) rejects torn reads in practice.
///
/// UplinkClient updates the block on every validated frame; between
/// frames it keeps the last good frame, and before the first frame it is
/// all zeros (a valid all-zero frame).
/// </summary>
public static class UplinkMemoryMarker
{
    public const int MagicOffset = 0;
    public const int FrameOffset = 8;
    public const int SeqlockOffset = FrameOffset + UplinkProtocol.FrameLen; // 62
    public const int BlockSize = SeqlockOffset + 4; // 66

    /// <summary>The 8-byte marker the scanner searches for ("PMUPLINK").</summary>
    public static readonly byte[] Magic =
    {
        0x50, 0x4D, 0x55, 0x50, 0x4C, 0x49, 0x4E, 0x4B,
    };

    public static readonly byte[] Block = CreateBlock();

    /// <summary>
    /// Copies one validated 54-byte wire frame into the block under the
    /// seqlock. Pass the exact frame bytes (StreamProcessor hands
    /// UplinkClient.OnFrame a fresh checksum-verified copy).
    /// </summary>
    public static void Update(ReadOnlySpan<byte> frame)
    {
        if (frame.Length != UplinkProtocol.FrameLen)
            throw new ArgumentException($"expected {UplinkProtocol.FrameLen} bytes", nameof(frame));

        BumpSeqlock(); // odd: copy in progress
        frame.CopyTo(Block.AsSpan(FrameOffset, UplinkProtocol.FrameLen));
        BumpSeqlock(); // even: snapshot stable
    }

    private static byte[] CreateBlock()
    {
        var block = new byte[BlockSize];
        Magic.CopyTo(block, MagicOffset);
        return block;
    }

    private static void BumpSeqlock()
    {
        var span = Block.AsSpan(SeqlockOffset, 4);
        int current = BinaryPrimitives.ReadInt32LittleEndian(span);
        BinaryPrimitives.WriteInt32LittleEndian(span, unchecked(current + 1));
    }

    [System.Runtime.CompilerServices.ModuleInitializer]
    internal static void InitializeMarker() => _ = Block;
}
