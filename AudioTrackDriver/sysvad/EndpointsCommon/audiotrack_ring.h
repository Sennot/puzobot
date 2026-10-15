// Audiotrack virtual cable: a single-producer / single-consumer byte ring that
// connects the render endpoint ("Audiotrack Out") to the capture endpoint
// ("Audiotrack Mic"). Anything played to the render endpoint comes out of the
// microphone. Both endpoints use the same fixed format (48000 Hz, stereo,
// 16-bit PCM), so the ring just moves bytes.
//
// Producer: CMiniportWaveRTStream::ReadBytes  (render stream, DPC/timer level)
// Consumer: CMiniportWaveRTStream::WriteBytes (capture stream, DPC/timer level)
// Each side owns exactly one index, so no lock is needed.
//
// Included by exactly one translation unit (minwavertstream.cpp).

#pragma once

#define AT_RING_SIZE            0x40000u            // 262144 bytes, power of two
#define AT_BYTES_PER_MS         192u                // 48000 * 2 ch * 2 bytes / 1000
#define AT_PRIME_BYTES          (AT_BYTES_PER_MS * 20u)   // start reading after 20 ms are queued
#define AT_TARGET_BYTES         (AT_BYTES_PER_MS * 40u)   // jump to 40 ms of queue when we fall behind
#define AT_MAX_LATENCY_BYTES    (AT_BYTES_PER_MS * 100u)  // more than this queued = stale, trim

static UCHAR          g_AtRing[AT_RING_SIZE];
static volatile LONG64 g_AtRingWritten = 0;   // total bytes ever written (producer owns)
static volatile LONG64 g_AtRingRead = 0;      // total bytes ever consumed (consumer owns)
static BOOLEAN        g_AtRingPrimed = FALSE; // consumer state

// Producer. When the ring is full (nobody is capturing) new data is dropped.
static VOID AtRingWrite(_In_reads_bytes_(Length) const UCHAR* Source, _In_ ULONG Length)
{
    LONG64 written = g_AtRingWritten;
    LONG64 read = g_AtRingRead;

    // Only whole 4-byte frames (stereo 16-bit): a partial frame, e.g. at the end
    // of a stream, would swap the left and right channels from then on.
    Length &= ~3u;
    if (Length == 0)
    {
        return;
    }

    if ((ULONGLONG)(written - read) + Length > AT_RING_SIZE)
    {
        return;
    }

    ULONG offset = (ULONG)(written & (AT_RING_SIZE - 1));
    ULONG first = min(Length, AT_RING_SIZE - offset);
    RtlCopyMemory(&g_AtRing[offset], Source, first);
    if (first < Length)
    {
        RtlCopyMemory(&g_AtRing[0], Source + first, Length - first);
    }

    KeMemoryBarrier();
    g_AtRingWritten = written + Length;
}

// Consumer. Always fills `Length` bytes; missing data becomes silence.
static VOID AtRingRead(_Out_writes_bytes_(Length) UCHAR* Destination, _In_ ULONG Length)
{
    LONG64 written = g_AtRingWritten;
    LONG64 read = g_AtRingRead;
    ULONGLONG available = (ULONGLONG)(written - read);

    if (!g_AtRingPrimed)
    {
        if (available < AT_PRIME_BYTES)
        {
            RtlZeroMemory(Destination, Length);
            return;
        }
        g_AtRingPrimed = TRUE;
    }

    // Far too much queued (the capture side was not running): keep the cable
    // latency bounded instead of playing old audio.
    if (available > AT_MAX_LATENCY_BYTES)
    {
        read = written - AT_TARGET_BYTES;
        available = AT_TARGET_BYTES;
    }

    ULONG toCopy = (available < Length) ? (ULONG)available : Length;
    if (toCopy > 0)
    {
        ULONG offset = (ULONG)(read & (AT_RING_SIZE - 1));
        ULONG first = min(toCopy, AT_RING_SIZE - offset);
        RtlCopyMemory(Destination, &g_AtRing[offset], first);
        if (first < toCopy)
        {
            RtlCopyMemory(Destination + first, &g_AtRing[0], toCopy - first);
        }
    }
    if (toCopy < Length)
    {
        // Underrun: pad with silence and wait for the queue to refill.
        RtlZeroMemory(Destination + toCopy, Length - toCopy);
        g_AtRingPrimed = FALSE;
    }

    KeMemoryBarrier();
    g_AtRingRead = read + toCopy;
}
