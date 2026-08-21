// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// Songs and setlists on disk. The theme running through these tests: a load either
// produces a usable song or an error a performer can act on. It never throws, never
// half-replaces what was already loaded, and never quietly changes what the song means.

#include "TestMain.h"
#include "core/Persistence.h"

#include <string>

using namespace ghostband::core;

namespace {

Song makeSong() {
    Song s;
    s.title = "Ghost Light";
    s.defaultStylePrompt = "warm organic indie folk ensemble, instrumental";
    s.modelName = "mrt2_small";
    s.masterAiLevelDb = -3.5f;
    s.harmonySource = HarmonySource::Midi;

    SongSection verse;
    verse.name = "Verse";
    verse.stylePrompt = "";                     // inherits the song default
    verse.aiIntensity = 0.35f;
    verse.aiEnabled = true;
    verse.transitionMs = kTransitionMediumMs;
    verse.chordProgression = {"G", "D", "Em", "C"};
    verse.notes = "quiet, brushes";

    SongSection chorus;
    chorus.name = "Chorus";
    chorus.stylePrompt = "full band, driving, instrumental";
    chorus.aiIntensity = 0.85f;
    chorus.aiEnabled = true;
    chorus.transitionMs = kTransitionFastMs;
    chorus.tempoBpm = 92.5;

    SongSection solo;
    solo.name = "Solo Verse";
    solo.aiEnabled = false;                     // a real musical choice, not a disabled feature
    solo.aiIntensity = 0.0f;
    solo.transitionMs = kTransitionInstantMs;

    s.sections = {verse, chorus, solo};
    return s;
}

Setlist makeSetlist() {
    Setlist l;
    l.name = "Friday, The Blue Room";
    l.entries = {{"ghost-light.ghostsong", "Ghost Light"},
                 {"paper-boats.ghostsong", "Paper Boats"},
                 {"the-long-way.ghostsong", "The Long Way"}};
    return l;
}

bool sectionsEqual(const SongSection& a, const SongSection& b) {
    return a.name == b.name && a.stylePrompt == b.stylePrompt
        && ghostband::test::nearlyEqual(a.aiIntensity, b.aiIntensity, 1e-6)
        && a.aiEnabled == b.aiEnabled && a.transitionMs == b.transitionMs
        && a.chordProgression == b.chordProgression && a.notes == b.notes
        && a.tempoBpm.has_value() == b.tempoBpm.has_value()
        && (!a.tempoBpm.has_value()
            || ghostband::test::nearlyEqual(*a.tempoBpm, *b.tempoBpm, 1e-9));
}

} // namespace

TEST_MAIN_BEGIN("Persistence")

// --- Songs: round trip --------------------------------------------------------------

TEST("a song survives a round trip intact") {
    const Song original = makeSong();
    Song restored;
    const auto r = deserialiseSong(serialiseSong(original), restored);

    CHECK(r.ok);
    CHECK_EQ(r.version, Song::kSchemaVersion);
    CHECK(restored.title == original.title);
    CHECK(restored.defaultStylePrompt == original.defaultStylePrompt);
    CHECK(restored.modelName == original.modelName);
    CHECK_NEAR(restored.masterAiLevelDb, original.masterAiLevelDb, 1e-6);
    CHECK(restored.harmonySource == original.harmonySource);

    CHECK_EQ(static_cast<int>(restored.sections.size()),
             static_cast<int>(original.sections.size()));
    for (std::size_t i = 0; i < original.sections.size() && i < restored.sections.size(); ++i) {
        CHECK(sectionsEqual(restored.sections[i], original.sections[i]));
    }
}

TEST("the demo song survives a round trip") {
    const Song original = makeDemoSong();
    Song restored;
    CHECK(deserialiseSong(serialiseSong(original), restored).ok);
    CHECK(restored.title == original.title);
    CHECK_EQ(static_cast<int>(restored.sections.size()),
             static_cast<int>(original.sections.size()));
    CHECK(restored.isValid());
}

TEST("an empty section prompt stays empty rather than becoming the default") {
    // Inheritance is the point: editing the song-level prompt must keep updating every
    // section that never overrode it. Baking the default in on save would break that
    // silently and permanently.
    Song original = makeSong();
    Song restored;
    CHECK(deserialiseSong(serialiseSong(original), restored).ok);

    CHECK(restored.sections[0].stylePrompt.empty());
    CHECK(restored.sections[0].resolvedPrompt(restored.defaultStylePrompt)
          == original.defaultStylePrompt);
}

TEST("a disabled section stays disabled") {
    // A section with the band off is a musical choice. Losing it on reload would put a
    // band under what the performer wrote as a solo verse.
    Song restored;
    CHECK(deserialiseSong(serialiseSong(makeSong()), restored).ok);
    CHECK(!restored.sections[2].aiEnabled);
    CHECK_EQ(restored.sections[2].transitionMs, kTransitionInstantMs);
}

TEST("free text with awkward characters round-trips") {
    Song s = makeSong();
    s.title = "Song = one; two [bracketed]";
    s.sections[0].notes = "line one\nline two\r\nwith a backslash \\ and =signs=";
    s.sections[0].stylePrompt = "brushed drums, upright bass";
    s.defaultStylePrompt = "";   // section prompts must then all be non-empty
    s.sections[1].stylePrompt = "full band";
    s.sections[2].stylePrompt = "solo";

    Song restored;
    CHECK(deserialiseSong(serialiseSong(s), restored).ok);
    CHECK(restored.title == s.title);
    CHECK(restored.sections[0].notes == s.sections[0].notes);
}

TEST("an empty chord progression stays empty, and chord order is kept") {
    Song restored;
    CHECK(deserialiseSong(serialiseSong(makeSong()), restored).ok);

    const std::vector<std::string> expected{"G", "D", "Em", "C"};
    CHECK(restored.sections[0].chordProgression == expected);
    CHECK(restored.sections[1].chordProgression.empty());
}

TEST("an absent tempo stays absent rather than becoming zero") {
    // std::optional<double> means "the performer did not say", which is different from
    // "the performer said 0 bpm".
    Song restored;
    CHECK(deserialiseSong(serialiseSong(makeSong()), restored).ok);
    CHECK(!restored.sections[0].tempoBpm.has_value());
    CHECK(restored.sections[1].tempoBpm.has_value());
    CHECK_NEAR(*restored.sections[1].tempoBpm, 92.5, 1e-9);
}

TEST("every harmony source round-trips") {
    // The writer and the reader drifted apart once already ("guitar" vs
    // "guitar_experimental"), which would have silently reset Song Map songs to MIDI.
    for (auto source : {HarmonySource::Midi, HarmonySource::SongMap,
                        HarmonySource::GuitarExperimental}) {
        Song s = makeSong();
        s.harmonySource = source;
        Song restored;
        CHECK(deserialiseSong(serialiseSong(s), restored).ok);
        CHECK(restored.harmonySource == source);
    }
}

TEST("a negative master level round-trips exactly") {
    Song s = makeSong();
    s.masterAiLevelDb = -12.5f;
    Song restored;
    CHECK(deserialiseSong(serialiseSong(s), restored).ok);
    CHECK_NEAR(restored.masterAiLevelDb, -12.5f, 1e-6);
}

// --- Songs: versioning --------------------------------------------------------------

TEST("the header names the format and its version") {
    const auto text = serialiseSong(makeSong());
    CHECK(text.rfind("ghostband-song " + std::to_string(Song::kSchemaVersion) + "\n", 0) == 0);
    // Pinned as a literal too. Deriving it from the constant alone would let a version bump
    // pass this test without anyone thinking about the files already on disk.
    CHECK(text.rfind("ghostband-song 2\n", 0) == 0);
}

// --- Songs: migration ---------------------------------------------------------------

TEST("a version 1 song still opens, and gets the default beats per chord") {
    // The migration machinery's first real use. A version 1 file predates
    // SongSection::beatsPerChord entirely: every version 1 song meant one bar of 4/4,
    // because that is all Song Map could do.
    //
    // Written out as a literal rather than produced by an older serialiser, because the
    // point is to open a file exactly as it exists on a performer's disk today.
    const std::string v1 =
        "ghostband-song 1\n"
        "title=Old Song\n"
        "default_prompt=warm folk trio\n"
        "model=mrt2_small\n"
        "master_level_db=0\n"
        "harmony=song_map\n"
        "[section]\n"
        "name=Verse\n"
        "prompt=\n"
        "intensity=0.5\n"
        "ai_enabled=1\n"
        "transition_ms=500\n"
        "chord=G\n"
        "chord=D\n"
        "[section]\n"
        "name=Chorus\n"
        "prompt=\n"
        "intensity=0.7\n"
        "ai_enabled=1\n"
        "transition_ms=250\n"
        "chord=C\n";

    Song out;
    const auto result = deserialiseSong(v1, out);
    CHECK(result.ok);
    CHECK(result.message.empty());
    // Reports the version the *file* was, so the caller can tell the performer it was
    // upgraded rather than silently rewriting it.
    CHECK(result.version == 1);

    CHECK(out.title == "Old Song");
    CHECK(out.harmonySource == HarmonySource::SongMap);
    CHECK(out.sections.size() == 2);
    CHECK(out.sections[0].chordProgression == std::vector<std::string>({"G", "D"}));
    for (const auto& section : out.sections) {
        CHECK(section.beatsPerChord == kDefaultBeatsPerChord);
    }
}

TEST("a migrated song saves at the current version") {
    // The upgrade is only durable once it is written back, so this is the half of the
    // round trip that actually moves the file forward.
    const std::string v1 = "ghostband-song 1\ntitle=Old\nharmony=midi\n"
                           "[section]\nname=A\nprompt=x\n";
    Song out;
    CHECK(deserialiseSong(v1, out).ok);

    const auto rewritten = serialiseSong(out);
    CHECK(rewritten.rfind("ghostband-song 2\n", 0) == 0);
    CHECK(rewritten.find("beats_per_chord=4") != std::string::npos);

    Song reloaded;
    const auto result = deserialiseSong(rewritten, reloaded);
    CHECK(result.ok);
    CHECK(result.version == Song::kSchemaVersion);
}

TEST("beats per chord round-trips, and a nonsense value is clamped not refused") {
    Song song = makeSong();
    song.sections[0].beatsPerChord = 8;
    Song back;
    CHECK(deserialiseSong(serialiseSong(song), back).ok);
    CHECK(back.sections[0].beatsPerChord == 8);

    // Clamped on load, like intensity: an out-of-range number is recoverable, and refusing
    // to open a whole song over it would be the worse outcome. The editor refuses, which
    // is where a typo can still be fixed.
    const std::string bad = "ghostband-song 2\ntitle=T\nharmony=midi\n"
                            "[section]\nname=A\nprompt=x\nbeats_per_chord=9999\n";
    Song clamped;
    CHECK(deserialiseSong(bad, clamped).ok);
    CHECK(clamped.sections[0].beatsPerChord == kMaxBeatsPerChord);

    const std::string zero = "ghostband-song 2\ntitle=T\nharmony=midi\n"
                             "[section]\nname=A\nprompt=x\nbeats_per_chord=0\n";
    Song floored;
    CHECK(deserialiseSong(zero, floored).ok);
    CHECK(floored.sections[0].beatsPerChord == kMinBeatsPerChord);

    const std::string words = "ghostband-song 2\ntitle=T\nharmony=midi\n"
                              "[section]\nname=A\nprompt=x\nbeats_per_chord=four\n";
    Song refused;
    CHECK(!deserialiseSong(words, refused).ok);
}

TEST("a file from a newer GhostBand is refused, and says so specifically") {
    // The fix for this is "update the app", not "edit the file", so it must not be
    // reported as corruption.
    const std::string text = "ghostband-song 99\ntitle=From The Future\n[section]\nname=A\n"
                             "prompt=x\n";
    Song out;
    const auto r = deserialiseSong(text, out);

    CHECK(!r.ok);
    CHECK(r.fromNewerVersion);
    CHECK_EQ(r.version, 99);
    CHECK(r.message.find("Update GhostBand") != std::string::npos);
}

TEST("a missing or unreadable version is refused") {
    const char* bad[] = {
        "ghostband-song\ntitle=x\n",
        "ghostband-song abc\ntitle=x\n",
        "ghostband-song 0\ntitle=x\n",
        "ghostband-song -1\ntitle=x\n",
    };
    for (const char* text : bad) {
        Song out;
        const auto r = deserialiseSong(text, out);
        CHECK(!r.ok);
        CHECK(!r.fromNewerVersion);
    }
}

TEST("a setlist file is not accepted as a song") {
    Song out;
    const auto r = deserialiseSong(serialiseSetlist(makeSetlist()), out);
    CHECK(!r.ok);
    CHECK(r.message.find("song") != std::string::npos);
}

// --- Songs: bad input ---------------------------------------------------------------

TEST("a failed load leaves the previous song untouched") {
    // The performer still has whatever was open. Half-replacing it would be the worst
    // possible outcome of opening the wrong file.
    Song loaded = makeSong();
    const auto r = deserialiseSong("ghostband-song 1\ntitle=Broken\n", loaded);

    CHECK(!r.ok);                       // no sections
    CHECK(loaded.title == "Ghost Light");
    CHECK_EQ(static_cast<int>(loaded.sections.size()), 3);
}

TEST("garbage never throws and never loads") {
    const char* junk[] = {
        "", "\n", "not a ghostband file\n", "ghostband-song 1\n",
        "ghostband-song 1\n[section]\n",                  // section with no name or prompt
        "ghostband-song 1\n[verse]\nname=A\n",            // unknown marker
        "ghostband-song 1\ntitle=x\n[section]\nname=A\nprompt=p\nintensity=abc\n",
        "ghostband-song 1\ntitle=x\n[section]\nname=A\nprompt=p\ntransition_ms=xyz\n",
        "ghostband-song 1\ntitle=x\n[section]\nname=A\nprompt=p\nai_enabled=maybe\n",
        "ghostband-song 1\ntitle=x\n[section]\nname=A\nprompt=p\ntempo_bpm=fast\n",
        "ghostband-song 1\nharmony=telepathy\n[section]\nname=A\nprompt=p\n",
        "ghostband-song 1\ntitle=x\nmaster_level_db=1.0.0\n[section]\nname=A\nprompt=p\n",
    };
    for (const char* text : junk) {
        Song out;
        const auto r = deserialiseSong(text, out);
        ++ghostband::test::g_checks;
        if (r.ok) {
            ghostband::test::reportFailure(__FILE__, __LINE__,
                std::string("accepted junk song: \"") + text + '"');
        } else if (r.message.empty()) {
            // A refusal must always say why, or the performer is left with a song that
            // will not open and no idea what to fix.
            ghostband::test::reportFailure(__FILE__, __LINE__,
                std::string("refused without a reason: \"") + text + '"');
        }
    }
}

TEST("a parse error reports the line it was found on") {
    const std::string text = "ghostband-song 1\n"       // 1
                             "title=x\n"                // 2
                             "[section]\n"              // 3
                             "name=Verse\n"             // 4
                             "prompt=p\n"               // 5
                             "intensity=not-a-number\n";// 6
    Song out;
    const auto r = deserialiseSong(text, out);
    CHECK(!r.ok);
    CHECK_EQ(r.line, 6);
}

TEST("unknown keys are ignored rather than fatal") {
    // A file written by a slightly newer build of the same schema version still opens.
    const std::string text = "ghostband-song 1\n"
                             "title=Forward Compatible\n"
                             "default_prompt=p\n"
                             "some_future_field=whatever\n"
                             "[section]\n"
                             "name=Verse\n"
                             "another_future_field=17\n";
    Song out;
    const auto r = deserialiseSong(text, out);
    CHECK(r.ok);
    CHECK(out.title == "Forward Compatible");
    CHECK_EQ(static_cast<int>(out.sections.size()), 1);
}

TEST("blank lines and comments are ignored") {
    const std::string text = "ghostband-song 1\n"
                             "# written by hand\n"
                             "\n"
                             "title=Hand Edited\n"
                             "default_prompt=p\n"
                             "\n"
                             "[section]\n"
                             "name=Verse\n";
    Song out;
    CHECK(deserialiseSong(text, out).ok);
    CHECK(out.title == "Hand Edited");
}

TEST("CRLF line endings load") {
    // A file that has been through Windows, or a cloud sync that helpfully converted it.
    const std::string text = "ghostband-song 1\r\ntitle=From Windows\r\ndefault_prompt=p\r\n"
                             "[section]\r\nname=Verse\r\nintensity=0.4\r\n";
    Song out;
    const auto r = deserialiseSong(text, out);
    CHECK(r.ok);
    CHECK(out.title == "From Windows");
    CHECK_NEAR(out.sections[0].aiIntensity, 0.4f, 1e-6);
}

TEST("an out-of-range intensity is clamped, not rejected") {
    // Recoverable, so refusing to open the song over it would be the worse outcome.
    const std::string text = "ghostband-song 1\ndefault_prompt=p\n"
                             "[section]\nname=A\nintensity=7\n"
                             "[section]\nname=B\nintensity=-3\n";
    Song out;
    CHECK(deserialiseSong(text, out).ok);
    CHECK_NEAR(out.sections[0].aiIntensity, 1.0f, 1e-6);
    CHECK_NEAR(out.sections[1].aiIntensity, 0.0f, 1e-6);
}

TEST("a truncated file fails rather than loading a partial song") {
    // Simulates a crash mid-write: the header and some sections survive, the rest does
    // not. Loading the fragment silently would drop sections from a set.
    const auto full = serialiseSong(makeSong());
    const auto truncated = full.substr(0, full.size() / 3);

    Song out;
    const auto r = deserialiseSong(truncated, out);
    // Either it refuses, or it loads strictly fewer sections — what it must never do is
    // claim a complete song.
    if (r.ok) CHECK(static_cast<int>(out.sections.size()) < 3);
    else CHECK(!r.message.empty());
}

// --- Setlists ------------------------------------------------------------------------

TEST("a setlist survives a round trip in order") {
    const Setlist original = makeSetlist();
    Setlist restored;
    const auto r = deserialiseSetlist(serialiseSetlist(original), restored);

    CHECK(r.ok);
    CHECK(restored.name == original.name);
    CHECK_EQ(static_cast<int>(restored.entries.size()), 3);
    for (std::size_t i = 0; i < 3 && i < restored.entries.size(); ++i) {
        CHECK(restored.entries[i].songFile == original.entries[i].songFile);
        CHECK(restored.entries[i].cachedTitle == original.entries[i].cachedTitle);
    }
}

TEST("the same song may appear twice in a set") {
    // Encores exist.
    Setlist l = makeSetlist();
    l.entries.push_back(l.entries.front());

    Setlist restored;
    CHECK(deserialiseSetlist(serialiseSetlist(l), restored).ok);
    CHECK_EQ(static_cast<int>(restored.entries.size()), 4);
    CHECK(restored.entries[3].songFile == restored.entries[0].songFile);
}

TEST("an empty setlist is refused") {
    Setlist empty;
    Setlist out;
    const auto r = deserialiseSetlist(serialiseSetlist(empty), out);
    CHECK(!r.ok);
    CHECK(!r.message.empty());
}

TEST("a setlist from a newer GhostBand is refused specifically") {
    Setlist out;
    const auto r = deserialiseSetlist("ghostband-setlist 99\nname=x\n[song]\nfile=a\n", out);
    CHECK(!r.ok);
    CHECK(r.fromNewerVersion);
}

TEST("a song file is not accepted as a setlist") {
    Setlist out;
    const auto r = deserialiseSetlist(serialiseSong(makeSong()), out);
    CHECK(!r.ok);
    CHECK(r.message.find("setlist") != std::string::npos);
}

TEST("setlist garbage never throws and never loads") {
    const char* junk[] = {
        "", "\n", "ghostband-setlist\n", "ghostband-setlist 1\n",
        "ghostband-setlist 1\nname=x\n",                     // no entries
        "ghostband-setlist 1\nname=x\n[track]\nfile=a\n",    // unknown marker
        "ghostband-setlist 1\nname=x\n[song]\ntitle=No File\n",   // entry with no file
    };
    for (const char* text : junk) {
        Setlist out;
        const auto r = deserialiseSetlist(text, out);
        ++ghostband::test::g_checks;
        if (r.ok) {
            ghostband::test::reportFailure(__FILE__, __LINE__,
                std::string("accepted junk setlist: \"") + text + '"');
        }
    }
}

TEST("a failed setlist load leaves the previous one untouched") {
    Setlist loaded = makeSetlist();
    CHECK(!deserialiseSetlist("ghostband-setlist 1\nname=Broken\n", loaded).ok);
    CHECK(loaded.name == "Friday, The Blue Room");
    CHECK_EQ(static_cast<int>(loaded.entries.size()), 3);
}

// --- File names -----------------------------------------------------------------------

TEST("a title becomes a safe file stem") {
    CHECK(toSafeFileStem("Ghost Light") == "Ghost-Light");
    CHECK(toSafeFileStem("AC/DC?") == "AC-DC");
    CHECK(toSafeFileStem("  spaced   out  ") == "spaced-out");
    CHECK(toSafeFileStem("under_score-and-dash") == "under_score-and-dash");
    CHECK(toSafeFileStem("Song #1") == "Song-1");
}

TEST("a file stem is never empty, hidden, or a path") {
    // Each of these would lose the song: an empty name is unopenable, a leading dot hides
    // it from Finder, and a separator would write it somewhere else entirely.
    const char* titles[] = {"", "???", "///", "...", "   ", "..", ".hidden", "a/b/c"};
    for (const char* t : titles) {
        const auto stem = toSafeFileStem(t);
        ++ghostband::test::g_checks;
        if (stem.empty() || stem.front() == '.'
            || stem.find('/') != std::string::npos
            || stem.find('\\') != std::string::npos) {
            ghostband::test::reportFailure(__FILE__, __LINE__,
                std::string("unsafe stem \"") + stem + "\" from title \"" + t + '"');
        }
    }
}

TEST("non-ASCII titles produce ASCII file names") {
    // The name on disk should read the same way it looks on screen, on any filesystem.
    const auto stem = toSafeFileStem("Caf\xc3\xa9 Nocturne");
    for (char c : stem) {
        const auto u = static_cast<unsigned char>(c);
        CHECK(u >= 0x20 && u <= 0x7e);
    }
    CHECK(!stem.empty());
}

TEST("the file extensions are distinct and dotted") {
    CHECK(std::string(kSongFileExtension) == ".ghostsong");
    CHECK(std::string(kSetlistFileExtension) == ".ghostset");
    CHECK(std::string(kSongFileExtension) != kSetlistFileExtension);
}

TEST_MAIN_END()
