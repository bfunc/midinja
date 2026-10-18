#include "MidiFileWriter.h"

#include <algorithm>
#include <cmath>

namespace midinja
{

namespace
{
constexpr int kPpq = 480;

void putVarLen (std::vector<uint8_t>& out, uint32_t v)
{
    uint8_t buf[5];
    int n = 0;
    buf[n++] = (uint8_t) (v & 0x7F);
    while ((v >>= 7) != 0)
        buf[n++] = (uint8_t) ((v & 0x7F) | 0x80);
    while (n > 0)
        out.push_back (buf[--n]);
}

void putBE (std::vector<uint8_t>& out, uint32_t v, int bytes)
{
    for (int i = bytes - 1; i >= 0; --i)
        out.push_back ((uint8_t) ((v >> (8 * i)) & 0xFF));
}

struct TickEvent
{
    uint32_t tick;
    bool on;
    uint8_t note;
    uint8_t vel;
};

void putTrackName (std::vector<uint8_t>& track, const std::string& name)
{
    putVarLen (track, 0);
    track.push_back (0xFF); track.push_back (0x03);
    putVarLen (track, (uint32_t) name.size());
    track.insert (track.end(), name.begin(), name.end());
}

void putTempoAndMeter (std::vector<uint8_t>& track, double bpm)
{
    const auto usPerBeat = (uint32_t) std::lround (60000000.0 / (bpm > 0 ? bpm : 120.0));
    putVarLen (track, 0);
    track.push_back (0xFF); track.push_back (0x51); track.push_back (0x03);
    putBE (track, usPerBeat, 3);
    putVarLen (track, 0);
    track.push_back (0xFF); track.push_back (0x58); track.push_back (0x04);
    track.push_back (4); track.push_back (2); track.push_back (24); track.push_back (8);
}

// The notes of one track, then the end of track at the cycle end (so a clip loops right).
void putNotes (std::vector<uint8_t>& track, const std::vector<NoteEvent>& notes, int channel,
               double ticksPerStep, double cycleSteps)
{
    const uint8_t ch = (uint8_t) (std::clamp (channel, 1, 16) - 1);
    std::vector<TickEvent> ev;
    for (const auto& n : notes)
    {
        const auto on = (uint32_t) std::lround (std::max (0.0, n.start) * ticksPerStep);
        auto off = (uint32_t) std::lround (std::max (0.0, n.start + n.length) * ticksPerStep);
        if (off <= on)
            off = on + 1;
        ev.push_back ({ on, true, (uint8_t) std::clamp (n.note, 0, 127), (uint8_t) std::clamp (n.velocity, 1, 127) });
        ev.push_back ({ off, false, (uint8_t) std::clamp (n.note, 0, 127), 0 });
    }
    std::stable_sort (ev.begin(), ev.end(), [] (const TickEvent& a, const TickEvent& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        return ! a.on && b.on; // offs before ons at the same tick
    });
    uint32_t last = 0;
    for (const auto& e : ev)
    {
        putVarLen (track, e.tick - last);
        last = e.tick;
        track.push_back ((uint8_t) ((e.on ? 0x90 : 0x80) | ch));
        track.push_back (e.note);
        track.push_back (e.on ? e.vel : (uint8_t) 64);
    }
    const auto endTick = std::max (last, (uint32_t) std::lround (std::max (0.0, cycleSteps) * ticksPerStep));
    putVarLen (track, endTick - last);
    track.push_back (0xFF); track.push_back (0x2F); track.push_back (0x00);
}

void putChunk (std::vector<uint8_t>& out, const std::vector<uint8_t>& track)
{
    out.push_back ('M'); out.push_back ('T'); out.push_back ('r'); out.push_back ('k');
    putBE (out, (uint32_t) track.size(), 4);
    out.insert (out.end(), track.begin(), track.end());
}
} // namespace

std::vector<uint8_t> writeMidiFile (const std::vector<NoteEvent>& notes, double stepsPerBeat,
                                    double bpm, int channel, const std::string& trackName,
                                    double cycleSteps)
{
    const double ticksPerStep = kPpq / (stepsPerBeat > 0 ? stepsPerBeat : 4.0);
    std::vector<uint8_t> track;
    putTrackName (track, trackName);
    putTempoAndMeter (track, bpm);
    putNotes (track, notes, channel, ticksPerStep, cycleSteps);

    std::vector<uint8_t> out { 'M', 'T', 'h', 'd' };
    putBE (out, 6, 4);
    putBE (out, 0, 2); // format 0
    putBE (out, 1, 2); // one track
    putBE (out, kPpq, 2);
    putChunk (out, track);
    return out;
}

std::vector<uint8_t> writeMidiFileTracks (const std::vector<MidiTrack>& tracks, double stepsPerBeat,
                                          double bpm, double cycleSteps)
{
    const double ticksPerStep = kPpq / (stepsPerBeat > 0 ? stepsPerBeat : 4.0);
    std::vector<uint8_t> out { 'M', 'T', 'h', 'd' };
    putBE (out, 6, 4);
    putBE (out, 1, 2); // format 1
    putBE (out, (uint32_t) tracks.size() + 1, 2);
    putBE (out, kPpq, 2);

    std::vector<uint8_t> tempo;
    putTrackName (tempo, "Midinja");
    putTempoAndMeter (tempo, bpm);
    putVarLen (tempo, 0);
    tempo.push_back (0xFF); tempo.push_back (0x2F); tempo.push_back (0x00);
    putChunk (out, tempo);

    for (const auto& t : tracks)
    {
        std::vector<uint8_t> track;
        putTrackName (track, t.name);
        putNotes (track, t.notes, t.channel, ticksPerStep, cycleSteps);
        putChunk (out, track);
    }
    return out;
}

} // namespace midinja
