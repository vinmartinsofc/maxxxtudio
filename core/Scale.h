#pragma once
#include <string_view>
#include <vector>

namespace roll
{
enum class Mode
{
    Major, NaturalMinor, Dorian, Phrygian, Lydian, Mixolydian, Locrian,
    HarmonicMinor, MelodicMinor
};

// Harmonic field/scale: tonic (0=C ... 11=B) + mode.
class Scale
{
public:
    Scale() = default;
    Scale(int rootPitchClass, Mode mode);

    int  root() const { return rootPc; }
    Mode mode() const { return m; }

    // Does this MIDI note (in any octave) belong to the scale?
    bool contains(int midiNote) const;

    // Scale degrees as pitch classes (0..11), starting from the tonic.
    std::vector<int> pitchClasses() const;

    static std::vector<int> intervalsOf(Mode mode);
    static std::string_view nameOf(Mode mode);

private:
    int  rootPc = 0;
    Mode m = Mode::Major;
};
} // namespace roll
