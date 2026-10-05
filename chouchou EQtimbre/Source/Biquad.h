#pragma once

#include <cmath>

namespace chouchou
{
    // Transposed direct form II biquad with RBJ cookbook designs.
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;

        void reset() noexcept { z1 = z2 = 0.0f; }

        float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        void setCoefficients (const Biquad& o) noexcept
        {
            b0 = o.b0; b1 = o.b1; b2 = o.b2; a1 = o.a1; a2 = o.a2;
        }

        static Biquad lowpass (double sampleRate, double hz, double q) noexcept
        {
            const double w0 = 2.0 * 3.14159265358979323846 * hz / sampleRate;
            const double c = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
            const double a0 = 1.0 + alpha;
            Biquad f;
            f.b0 = (float) ((1.0 - c) * 0.5 / a0);
            f.b1 = (float) ((1.0 - c) / a0);
            f.b2 = f.b0;
            f.a1 = (float) (-2.0 * c / a0);
            f.a2 = (float) ((1.0 - alpha) / a0);
            return f;
        }

        // Constant 0 dB peak: unity gain and zero phase at the centre frequency.
        static Biquad bandpass (double sampleRate, double hz, double q) noexcept
        {
            const double w0 = 2.0 * 3.14159265358979323846 * hz / sampleRate;
            const double c = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
            const double a0 = 1.0 + alpha;
            Biquad f;
            f.b0 = (float) (alpha / a0);
            f.b1 = 0.0f;
            f.b2 = (float) (-alpha / a0);
            f.a1 = (float) (-2.0 * c / a0);
            f.a2 = (float) ((1.0 - alpha) / a0);
            return f;
        }
    };
}
