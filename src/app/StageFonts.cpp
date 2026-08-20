// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#include "StageType.h"

#include <BinaryData.h>

namespace ghostband::app {

juce::Typeface::Ptr displayTypeface() {
    // Loaded once and kept alive for the process. Building a Typeface parses the whole
    // font, and displayFont() is called from paint() — several times per row, at 10 Hz on
    // the setup screen — so doing it per call would be a parse per frame.
    //
    // Function-local static rather than a namespace-scope one: this must not be
    // constructed before JUCE's font subsystem exists, which a static initialiser at
    // namespace scope cannot guarantee.
    static const juce::Typeface::Ptr typeface = juce::Typeface::createSystemTypefaceFor(
        BinaryData::AntonRegular_ttf, BinaryData::AntonRegular_ttfSize);
    return typeface;
}

} // namespace ghostband::app
