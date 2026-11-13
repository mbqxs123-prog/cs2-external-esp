#pragma once
// DriverLoader.h -- maps the embedded cs2core.sys on first launch.
//
// The exe carries two RCDATA blobs:
//   IDR_KDMAPPER : kdmapper build with the embedded signed trampoline
//   IDR_DRIVER   : cs2core.sys
// They are written to a random temp directory, executed, and deleted. Nothing
// is installed as a service and no registry value is written.

namespace DriverLoader
{
    // True when the shared frame section exists and its header validates.
    // This is the only reliable "driver is live" signal: the driver no longer
    // writes a file by default.
    bool Present();

    // Map the driver unless Present() is already true (or force == true).
    // Requires an elevated token; the manifest asks for it at launch.
    bool Ensure(bool force);

    bool IsElevated();

    const char* LastError();

    // Sizes of the embedded blobs (0 when a resource is missing).
    unsigned MapperBytes();
    unsigned DriverBytes();
}
