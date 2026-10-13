#pragma once

// The built-in preview synth: mono, saw or square (polyBLEP), a resonant low-pass with a little
// envelope on the cutoff, and an amp envelope. Plain C++, header only.

#include <algorithm>
#include <cmath>

namespace briffer
{

class MonoSynth
{
public:
    enum class Wave { Saw = 0, Square };

    void prepare (double sampleRate)
    {
        sr = sampleRate > 0 ? sampleRate : 44100.0;
        reset();
    }

    void reset()
    {
        phase = 0.0;
        env = 0.0;
        gate = false;
        current = -1;
        ic1 = ic2 = 0.0;
        cutoffSmooth = cutoffHz;
    }

    // Parameters, set once per block.
    Wave wave = Wave::Saw;
    double cutoffHz = 1800.0; // 40..12000
    double resonance = 0.3;   // 0..1
    double decayMs = 250.0;   // 30..1500
    double volume = 0.6;      // 0..1

    void noteOn (int note, int velocity)
    {
        current = note;
        freq = 440.0 * std::pow (2.0, (note - 69) / 12.0);
        vel = std::clamp (velocity, 1, 127) / 127.0;
        gate = true;
        stage = Stage::Attack;
    }

    void noteOff (int note)
    {
        if (note == current)
        {
            gate = false;
            stage = Stage::Release;
        }
    }

    void allOff()
    {
        gate = false;
        stage = Stage::Release;
        current = -1;
    }

    // Adds into the two channel buffers.
    void render (float* left, float* right, int start, int num)
    {
        const double attackInc = 1.0 / (0.002 * sr);
        const double decayCoef = std::exp (-1.0 / (std::max (30.0, decayMs) * 0.001 * sr));
        const double releaseCoef = std::exp (-1.0 / (0.08 * sr));
        const double sustain = 0.35;
        const double k = 2.0 - 1.95 * std::clamp (resonance, 0.0, 1.0); // SVF damping

        for (int i = start; i < start + num; ++i)
        {
            switch (stage)
            {
                case Stage::Attack:
                    env += attackInc;
                    if (env >= 1.0) { env = 1.0; stage = Stage::Decay; }
                    break;
                case Stage::Decay:
                    env = sustain + (env - sustain) * decayCoef;
                    break;
                case Stage::Release:
                    env *= releaseCoef;
                    if (env < 1.0e-5) { env = 0.0; stage = Stage::Idle; }
                    break;
                case Stage::Idle:
                    break;
            }
            if (stage == Stage::Idle)
                continue;

            // Oscillator with polyBLEP.
            const double dt = freq / sr;
            double s;
            if (wave == Wave::Saw)
            {
                s = 2.0 * phase - 1.0 - blep (phase, dt);
            }
            else
            {
                s = phase < 0.5 ? 1.0 : -1.0;
                s += blep (phase, dt);
                s -= blep (std::fmod (phase + 0.5, 1.0), dt);
            }
            phase += dt;
            if (phase >= 1.0)
                phase -= 1.0;

            // Cutoff: smoothed knob, opened up to ~2 octaves by the envelope.
            cutoffSmooth += (cutoffHz - cutoffSmooth) * 0.002;
            const double fc = std::clamp (cutoffSmooth * std::pow (2.0, 2.0 * env * vel), 20.0, sr * 0.45);
            const double g = std::tan (3.14159265358979 * fc / sr);
            const double a1 = 1.0 / (1.0 + g * (g + k));
            const double a2 = g * a1;
            const double a3 = g * a2;
            const double v3 = s - ic2;
            const double v1 = a1 * ic1 + a2 * v3;
            const double v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0 * v1 - ic1;
            ic2 = 2.0 * v2 - ic2;

            const auto out = (float) (v2 * env * (0.4 + 0.6 * vel) * volume * 0.5);
            left[i] += out;
            right[i] += out;
        }
    }

private:
    enum class Stage { Idle, Attack, Decay, Release };

    static double blep (double t, double dt)
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
        return 0.0;
    }

    double sr = 44100.0;
    double phase = 0.0, freq = 110.0, vel = 1.0, env = 0.0;
    double ic1 = 0.0, ic2 = 0.0, cutoffSmooth = 1800.0;
    bool gate = false;
    int current = -1;
    Stage stage = Stage::Idle;
};

} // namespace briffer
