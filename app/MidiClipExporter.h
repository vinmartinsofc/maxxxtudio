#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Clip.h"

namespace midi_clip_export
{
juce::Result write (const roll::Clip&, double bpm, juce::OutputStream&);
}
