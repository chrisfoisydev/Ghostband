// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#pragma once

#include "Setlist.h"
#include "Song.h"

#include <string>

namespace ghostband::core {

/// Songs and setlists on disk.
///
/// **Why not JSON.** A parser is the part of a file format most likely to throw, and this
/// one runs when a performer opens a song — sometimes at soundcheck, sometimes between
/// songs. The format here is line-oriented and its parser has exactly one failure mode:
/// return an error. It is also readable and repairable in any text editor, which matters
/// the one time a file is half-written by a crash.
///
/// Shape:
/// ```
/// ghostband-song 1
/// title=Ghost Light
/// default_prompt=warm organic indie folk ensemble, instrumental
/// [section]
/// name=Verse
/// intensity=0.35
/// chord=G
/// chord=D
/// ```
/// The first line names the kind and the schema version. `[section]` opens a new section;
/// keys before the first one are song-level. Repeated keys (`chord`) append. Unknown keys
/// are ignored, so a file written by a slightly newer build of the same schema version
/// still loads.
///
/// **Numbers are always written and read in the C locale.** `std::to_string(0.35f)`
/// produces "0,35" under a French system locale and would then read back as 0 — a song
/// that quietly loses its intensities when carried to a different laptop.

/// Outcome of a load. Never throws; failure is always a returned value.
struct LoadResult {
    bool ok = true;
    /// Performer-readable, ASCII, and specific enough to act on.
    std::string message;
    /// 1-based line the problem was found on. 0 when not line-specific.
    int line = 0;
    /// Schema version found in the header, when one was readable.
    int version = 0;
    /// True when the file was written by a newer GhostBand than this one. Distinguished
    /// from ordinary corruption because the fix is completely different: update the app,
    /// do not edit the file.
    bool fromNewerVersion = false;

    static LoadResult success(int v) { return {true, {}, 0, v, false}; }
    static LoadResult failure(std::string msg, int lineNumber = 0) {
        return {false, std::move(msg), lineNumber, 0, false};
    }
};

/// @name Songs
/// @{
std::string serialiseSong(const Song& song);

/// Parse a song. `out` is left untouched unless the result is ok.
LoadResult deserialiseSong(const std::string& text, Song& out);
/// @}

/// @name Setlists
/// @{
std::string serialiseSetlist(const Setlist& list);
LoadResult deserialiseSetlist(const std::string& text, Setlist& out);
/// @}

/// The file-name suffixes GhostBand writes. Kept here so the app layer cannot drift.
inline constexpr const char* kSongFileExtension = ".ghostsong";
inline constexpr const char* kSetlistFileExtension = ".ghostset";

/// Turn a title into a safe file name stem: ASCII, no separators, never empty.
///
/// A song called "AC/DC?" must not create a directory or vanish into a hidden file, and a
/// title of only punctuation must still produce something openable.
std::string toSafeFileStem(const std::string& title);

} // namespace ghostband::core
