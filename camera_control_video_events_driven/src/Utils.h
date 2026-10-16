#pragma once

#include <string>
#include <ctime>

// Generic utilities: directory creation, time formatting, string helpers
namespace Utils
{
    // Recursively create a directory (Linux mkdir -p semantics); returns true
    //    if the directory already exists
    bool ensureDirectory(const std::string& path);

    // Current local time
    std::tm localTimeNow();

    // "YYYYMMDDHHMMSS"
    std::string formatTimeCompact(const std::tm& t);

    // Join two path components (handles redundant '/')
    std::string joinPath(const std::string& a, const std::string& b);

    // File size in bytes; returns -1 on failure
    long long fileSize(const std::string& path);

    // Compute file SHA-256 (64-char lowercase hex); returns empty on failure
    std::string fileSha256(const std::string& path);

    // Directory containing the executable (e.g. /opt/app/install/bin);
    // returns empty on failure
    std::string exeDir();

    // Application root: if the executable lives at <root>/bin/, returns <root>
    //    (logs/videos are siblings of bin/); if the executable is NOT under
    //    bin/ (e.g. running from a dev build/ folder), returns the exe
    //    directory itself.
    std::string appRootDir();

    // Fallback: derive the exe directory from argv[0] when /proc/self/exe is
    //    unavailable (chroot / container / seccomp). argv0 must be an
    //    absolute path; returns empty otherwise.
    std::string exeDirFromArgv0(const char* argv0);
}
