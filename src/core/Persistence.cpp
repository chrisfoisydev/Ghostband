// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#include "Persistence.h"

#include <locale>
#include <sstream>

namespace ghostband::core {

namespace {

constexpr const char* kSongHeader = "ghostband-song";
constexpr const char* kSetlistHeader = "ghostband-setlist";
constexpr const char* kSectionMarker = "[section]";
constexpr const char* kEntryMarker = "[song]";

/// Values may contain newlines — a section's notes field is free text — so they are
/// escaped rather than the format being made multi-line. Backslash first, always, or
/// unescaping becomes order-dependent.
std::string escape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default:   out += c; break;
        }
    }
    return out;
}

std::string unescape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '\\' || i + 1 >= in.size()) {
            out += in[i];
            continue;
        }
        switch (in[++i]) {
            case 'n':  out += '\n'; break;
            case 'r':  out += '\r'; break;
            case '\\': out += '\\'; break;
            // A stray escape keeps both characters rather than eating the next one. This
            // is a hand-editable file; silently deleting a character would be worse than
            // preserving something odd.
            default:   out += '\\'; out += in[i]; break;
        }
    }
    return out;
}

/// Always the C locale. A comma decimal separator would make songs unportable between
/// machines — see the note in Persistence.h.
std::string formatDouble(double v) {
    std::ostringstream os;
    os.imbue(std::locale::classic());
    os << v;
    return os.str();
}

bool parseDouble(const std::string& s, double& out) {
    if (s.empty()) return false;
    std::istringstream is(s);
    is.imbue(std::locale::classic());
    double v = 0.0;
    is >> v;
    if (is.fail()) return false;

    // Trailing junk means the value is not what the writer meant. "0.35x" is a corrupt
    // line, not 0.35.
    char extra = 0;
    if (is >> extra) return false;
    out = v;
    return true;
}

bool parseInt(const std::string& s, int& out) {
    if (s.empty()) return false;
    std::size_t i = (s[0] == '-') ? 1 : 0;
    if (i >= s.size()) return false;
    for (; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
    }
    try {
        out = std::stoi(s);
        return true;
    } catch (...) {
        return false;   // out of range; a corrupt file must not throw into a live app
    }
}

bool parseBool(const std::string& s, bool& out) {
    if (s == "1" || s == "true")  { out = true;  return true; }
    if (s == "0" || s == "false") { out = false; return true; }
    return false;
}

std::string trimCarriageReturn(std::string line) {
    // Files edited on Windows, or carried through one, arrive with CRLF. Tolerating it
    // costs one line here and saves a load failure that would look like corruption.
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line;
}

/// Parse the "ghostband-song 1" header.
LoadResult parseHeader(const std::string& line, const char* expectedKind,
                       int supportedVersion) {
    std::istringstream is(line);
    std::string kind;
    std::string version_token;
    is >> kind >> version_token;

    if (kind != expectedKind) {
        return LoadResult::failure(std::string("Not a GhostBand ")
                                       + (std::string(expectedKind) == kSongHeader
                                              ? "song" : "setlist")
                                       + " file.", 1);
    }

    int version = 0;
    if (!parseInt(version_token, version) || version < 1) {
        return LoadResult::failure("File header has no readable version number.", 1);
    }

    if (version > supportedVersion) {
        LoadResult r = LoadResult::failure(
            "Saved by a newer version of GhostBand (format " + std::to_string(version)
                + ", this build reads " + std::to_string(supportedVersion)
                + "). Update GhostBand to open it.", 1);
        r.version = version;
        r.fromNewerVersion = true;
        return r;
    }
    return LoadResult::success(version);
}

/// Bring a parsed document up to the current schema.
///
/// Version 1 is the first format, so there is nothing to migrate yet and this returns
/// success unchanged. It exists now rather than later on purpose: the moment a schema
/// change is needed, the alternative is inventing migration under time pressure against
/// files that already hold a performer's set. Adding version 2 means adding one branch
/// here and one test that loads a real version 1 file.
template <typename T>
LoadResult migrate(T& /*value*/, int fromVersion, int toVersion) {
    if (fromVersion == toVersion) return LoadResult::success(fromVersion);

    // Unreachable today: parseHeader rejects anything above `toVersion`, and version 1 is
    // the floor. Reported honestly rather than silently accepted, because a version we do
    // not know how to migrate is not a version we can safely open.
    return LoadResult::failure("No migration from format version "
                               + std::to_string(fromVersion) + " to "
                               + std::to_string(toVersion) + ".", 1);
}

struct Line {
    std::string key;
    std::string value;
    bool isMarker = false;
    std::string marker;
};

/// Split one line into a marker, a key=value pair, or nothing.
bool classify(const std::string& raw, Line& out) {
    if (raw.empty()) return false;
    if (raw[0] == '#') return false;   // comments, so a file can be annotated by hand

    if (raw.front() == '[' && raw.back() == ']') {
        out.isMarker = true;
        out.marker = raw;
        return true;
    }

    const auto eq = raw.find('=');
    if (eq == std::string::npos) return false;

    out.isMarker = false;
    out.key = raw.substr(0, eq);
    out.value = unescape(raw.substr(eq + 1));
    return true;
}

void appendField(std::ostringstream& os, const char* key, const std::string& value) {
    os << key << '=' << escape(value) << '\n';
}

} // namespace

// -----------------------------------------------------------------------------------
// Songs
// -----------------------------------------------------------------------------------

std::string serialiseSong(const Song& song) {
    std::ostringstream os;
    os.imbue(std::locale::classic());

    os << kSongHeader << ' ' << Song::kSchemaVersion << '\n';
    appendField(os, "title", song.title);
    appendField(os, "default_prompt", song.defaultStylePrompt);
    appendField(os, "model", song.modelName);
    os << "master_level_db=" << formatDouble(static_cast<double>(song.masterAiLevelDb)) << '\n';
    os << "harmony=" << toString(song.harmonySource) << '\n';

    for (const auto& s : song.sections) {
        os << kSectionMarker << '\n';
        appendField(os, "name", s.name);
        appendField(os, "prompt", s.stylePrompt);
        os << "intensity=" << formatDouble(static_cast<double>(s.aiIntensity)) << '\n';
        os << "ai_enabled=" << (s.aiEnabled ? '1' : '0') << '\n';
        os << "transition_ms=" << s.transitionMs << '\n';
        for (const auto& chord : s.chordProgression) {
            appendField(os, "chord", chord);
        }
        if (s.tempoBpm.has_value()) {
            os << "tempo_bpm=" << formatDouble(*s.tempoBpm) << '\n';
        }
        if (!s.notes.empty()) appendField(os, "notes", s.notes);
    }
    return os.str();
}

LoadResult deserialiseSong(const std::string& text, Song& out) {
    std::istringstream is(text);
    std::string raw;

    if (!std::getline(is, raw)) {
        return LoadResult::failure("File is empty.", 1);
    }
    const auto header = parseHeader(trimCarriageReturn(raw), kSongHeader,
                                    Song::kSchemaVersion);
    if (!header.ok) return header;

    // Built into a local and only assigned to `out` on success, so a failed load cannot
    // leave the performer with a half-replaced song.
    Song song;
    song.sections.clear();
    bool in_section = false;
    int line_number = 1;

    while (std::getline(is, raw)) {
        ++line_number;
        Line line;
        if (!classify(trimCarriageReturn(raw), line)) continue;

        if (line.isMarker) {
            if (line.marker != kSectionMarker) {
                return LoadResult::failure("Unknown section marker " + line.marker + ".",
                                           line_number);
            }
            song.sections.emplace_back();
            in_section = true;
            continue;
        }

        if (!in_section) {
            if (line.key == "title")               song.title = line.value;
            else if (line.key == "default_prompt") song.defaultStylePrompt = line.value;
            else if (line.key == "model")          song.modelName = line.value;
            else if (line.key == "master_level_db") {
                double v = 0.0;
                if (!parseDouble(line.value, v)) {
                    return LoadResult::failure("master_level_db is not a number.",
                                               line_number);
                }
                song.masterAiLevelDb = static_cast<float>(v);
            } else if (line.key == "harmony") {
                // Falling back to MIDI would silently change how the song is steered.
                if (!parseHarmonySource(line.value, song.harmonySource)) {
                    return LoadResult::failure("Unknown harmony source '" + line.value + "'.",
                                               line_number);
                }
            }
            // Unknown song-level keys are ignored: forward compatibility within a version.
            continue;
        }

        auto& s = song.sections.back();
        if (line.key == "name")        s.name = line.value;
        else if (line.key == "prompt") s.stylePrompt = line.value;
        else if (line.key == "notes")  s.notes = line.value;
        else if (line.key == "chord")  s.chordProgression.push_back(line.value);
        else if (line.key == "intensity") {
            double v = 0.0;
            if (!parseDouble(line.value, v)) {
                return LoadResult::failure("intensity is not a number.", line_number);
            }
            // Clamped rather than rejected: an out-of-range intensity is recoverable and
            // refusing to open the song over it would be the worse outcome.
            s.aiIntensity = static_cast<float>(v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v));
        } else if (line.key == "ai_enabled") {
            if (!parseBool(line.value, s.aiEnabled)) {
                return LoadResult::failure("ai_enabled must be 0 or 1.", line_number);
            }
        } else if (line.key == "transition_ms") {
            int v = 0;
            if (!parseInt(line.value, v)) {
                return LoadResult::failure("transition_ms is not a number.", line_number);
            }
            s.transitionMs = v < 0 ? 0 : (v > kMaxTransitionMs ? kMaxTransitionMs : v);
        } else if (line.key == "tempo_bpm") {
            double v = 0.0;
            if (!parseDouble(line.value, v)) {
                return LoadResult::failure("tempo_bpm is not a number.", line_number);
            }
            s.tempoBpm = v;
        }
    }

    const auto migrated = migrate(song, header.version, Song::kSchemaVersion);
    if (!migrated.ok) return migrated;

    // A song with no sections cannot be performed, and finding that out at load time is
    // far better than at the first section change.
    if (const auto why = song.validate(); !why.empty()) {
        return LoadResult::failure(why);
    }

    out = std::move(song);
    return LoadResult::success(header.version);
}

// -----------------------------------------------------------------------------------
// Setlists
// -----------------------------------------------------------------------------------

std::string serialiseSetlist(const Setlist& list) {
    std::ostringstream os;
    os.imbue(std::locale::classic());

    os << kSetlistHeader << ' ' << Setlist::kSchemaVersion << '\n';
    appendField(os, "name", list.name);

    for (const auto& e : list.entries) {
        os << kEntryMarker << '\n';
        appendField(os, "file", e.songFile);
        appendField(os, "title", e.cachedTitle);
    }
    return os.str();
}

LoadResult deserialiseSetlist(const std::string& text, Setlist& out) {
    std::istringstream is(text);
    std::string raw;

    if (!std::getline(is, raw)) {
        return LoadResult::failure("File is empty.", 1);
    }
    const auto header = parseHeader(trimCarriageReturn(raw), kSetlistHeader,
                                    Setlist::kSchemaVersion);
    if (!header.ok) return header;

    Setlist list;
    list.entries.clear();
    bool in_entry = false;
    int line_number = 1;

    while (std::getline(is, raw)) {
        ++line_number;
        Line line;
        if (!classify(trimCarriageReturn(raw), line)) continue;

        if (line.isMarker) {
            if (line.marker != kEntryMarker) {
                return LoadResult::failure("Unknown marker " + line.marker + ".",
                                           line_number);
            }
            list.entries.emplace_back();
            in_entry = true;
            continue;
        }

        if (!in_entry) {
            if (line.key == "name") list.name = line.value;
            continue;
        }

        auto& e = list.entries.back();
        if (line.key == "file")       e.songFile = line.value;
        else if (line.key == "title") e.cachedTitle = line.value;
    }

    const auto migrated = migrate(list, header.version, Setlist::kSchemaVersion);
    if (!migrated.ok) return migrated;

    if (const auto why = list.validate(); !why.empty()) {
        return LoadResult::failure(why);
    }

    out = std::move(list);
    return LoadResult::success(header.version);
}

// -----------------------------------------------------------------------------------

std::string toSafeFileStem(const std::string& title) {
    std::string out;
    out.reserve(title.size());

    for (char c : title) {
        const auto u = static_cast<unsigned char>(c);
        const bool safe = (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z')
                       || (u >= '0' && u <= '9') || c == ' ' || c == '-' || c == '_';
        // Everything else becomes '-', including path separators and non-ASCII bytes.
        // "AC/DC" must not create a directory, and a UTF-8 title must not produce a name
        // that behaves differently on APFS than it reads on screen.
        out += safe ? c : '-';
    }

    // Collapse runs and trim, so "?? ?" does not become "-----".
    std::string collapsed;
    for (char c : out) {
        const bool separator = (c == '-' || c == ' ');
        if (separator && (collapsed.empty() || collapsed.back() == '-')) continue;
        collapsed += separator ? '-' : c;
    }
    while (!collapsed.empty() && collapsed.back() == '-') collapsed.pop_back();

    // A leading dot would make the file invisible in Finder, which for a performer means
    // the song is simply gone.
    if (collapsed.empty()) return "untitled";
    return collapsed;
}

} // namespace ghostband::core
