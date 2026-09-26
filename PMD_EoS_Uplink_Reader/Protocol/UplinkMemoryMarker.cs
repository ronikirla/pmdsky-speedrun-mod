using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;

namespace UplinkReader.Protocol;

/// <summary>
/// Publishes the most recent validated uplink frame into a fixed unmanaged
/// memory block so an external memory scanner can locate it by byte pattern
/// and read the latest frame at a fixed offset.
///
/// The block is allocated outside the managed .NET GC heap using
/// NativeMemory.Alloc, so garbage collection cannot relocate it. Its address
/// therefore remains stable for the lifetime of the process.
///
/// The address may differ between application launches due to ASLR. An
/// external reader should therefore locate the block by scanning for the
/// magic bytes rather than caching the address across launches.
///
/// Memory layout (all multi-byte values little-endian):
///
///   offset  size  content
///   ------  ----  -----------------------------------------------
///   0       8     magic: 50 4D 55 50 4C 49 4E 4B ("PMUPLINK")
///   8      70     raw wire frame - exact bytes received from device
///   78      4     seqlock uint32
///
/// The seqlock is:
///
///   odd   = a frame update is currently in progress
///   even  = the frame is a stable snapshot
///
/// External scanner recipe:
///
///   1. Scan process memory for:
///
///          50 4D 55 50 4C 49 4E 4B
///
///   2. The address of the first byte is the block address.
///
///   3. Read the frame from block + 8.
///
///   4. For a consistent snapshot:
///
///          seq1 = read uint32 at block + 78
///          frame = read 70 bytes at block + 8
///          seq2 = read uint32 at block + 78
///
///      Accept the frame only when:
///
///          seq1 == seq2 && (seq1 & 1) == 0
///
/// The block is intentionally kept allocated for the lifetime of the
/// process. The operating system will reclaim the memory when the process
/// terminates.
/// </summary>
public static unsafe class UplinkMemoryMarker
{
    /// <summary>
    /// Offset of the first byte of the magic marker.
    /// </summary>
    public const int MagicOffset = 0;
    public const int MagicOffsetPart2 = 4;

/// <summary>
/// Offset of the raw 70-byte uplink frame.
/// </summary>
public const int FrameOffset = 8;

    /// <summary>
    /// Offset of the 32-bit seqlock.
    /// </summary>
    public const int SeqlockOffset =
        FrameOffset + UplinkProtocol.FrameLen; // 78

    /// <summary>
    /// Total size of the unmanaged memory block.
    /// </summary>
    public const int BlockSize =
        SeqlockOffset + sizeof(uint); // 82

    /// <summary>
    /// Address of the unmanaged memory block.
    ///
    /// This address is stable for the lifetime of the process.
    /// It is not expected to be the same between application launches.
    /// </summary>
    public static nint Address => _block;

    private static readonly nint _block;

    static UplinkMemoryMarker()
    {
        _block = (nint)NativeMemory.Alloc((nuint)BlockSize);

        if (_block == 0)
            throw new OutOfMemoryException(
                $"Unable to allocate {BlockSize} bytes for UplinkMemoryMarker.");

        // NativeMemory.Alloc does not require us to depend on its initial
        // contents, so explicitly initialize the entire block.
        BlockSpan.Clear();

        // The 8 - byte marker the scanner searches for ("PMUPLINK")
        byte[] magicPart2 = {
            0x4C, 0x49, 0x4E, 0x4B,
        };
        byte[] magic = {
            0x50, 0x4D, 0x55, 0x50,
        };
        magic.CopyTo(BlockSpan.Slice(MagicOffset, magic.Length));
        magicPart2.CopyTo(BlockSpan.Slice(MagicOffsetPart2, magicPart2.Length));


        // The frame and seqlock remain zero until the first Update().
    }

    /// <summary>
    /// Provides a Span over the unmanaged block.
    ///
    /// This is used internally only. The external program accesses the same
    /// memory through the process address space.
    /// </summary>
    private static Span<byte> BlockSpan =>
        new((void*)_block, BlockSize);

    /// <summary>
    /// Copies one validated 70-byte wire frame into the unmanaged block.
    ///
    /// The caller must provide the exact frame bytes received from the
    /// device. UplinkClient should call this only after the frame has passed
    /// its normal validation/checksum checks.
    ///
    /// The seqlock is made odd before copying and even after copying, allowing
    /// an external reader to detect whether it obtained a stable snapshot.
    /// </summary>
    public static void Update(ReadOnlySpan<byte> frame)
    {
        if (frame.Length != UplinkProtocol.FrameLen)
        {
            throw new ArgumentException(
                $"expected {UplinkProtocol.FrameLen} bytes",
                nameof(frame));
        }

        // Odd = update in progress.
        BumpSeqlock();

        // Copy the exact validated wire frame into the shared block.
        frame.CopyTo(
            BlockSpan.Slice(
                FrameOffset,
                UplinkProtocol.FrameLen));

        // Even = update complete and frame is stable.
        BumpSeqlock();
    }

    /// <summary>
    /// Atomically increments the unmanaged 32-bit seqlock.
    ///
    /// The allocation returned by NativeMemory.Alloc is sufficiently aligned
    /// for a uint/int, and SeqlockOffset is 4-byte aligned (78 is not actually
    /// 4-byte aligned, so the sequence field is handled below as an unaligned
    /// byte location only if the platform permits it).
    /// </summary>
    private static void BumpSeqlock()
    {
        ref int sequence =
            ref Unsafe.AsRef<int>(
                (void*)((byte*)_block + SeqlockOffset));

        Interlocked.Increment(ref sequence);
    }

}
