#pragma once
// FrameShm.h -- read-only view of the driver's shared frame section.
//
// The display side never opens cs2.exe. It only asks the process list for the
// pid (a Toolhelp snapshot, no handle to the game), derives the section name
// from that pid and maps it read-only. When no game is running it falls back
// to pid 0, which is where the driver publishes diagnostic frames.

namespace FrameShm
{
    // Map the section (or remap it when the game pid changed). Cheap after the
    // first success; the process list is only re-read every 2 s.
    bool Ensure();

    // Drop the mapping. The next Ensure() re-resolves the pid.
    void Detach();

    bool Alive();
    unsigned Pid();

    // Seqlock snapshot: returns false when the producer is mid-write or when
    // the sequence moved while copying. dst must hold `len` bytes.
    bool Read(void* dst, int len);

    // Section name for a given pid and channel generation (exposed for
    // --shm-info style diagnostics).
    void BuildName(unsigned pid, unsigned salt, wchar_t* out, int cch);
}
