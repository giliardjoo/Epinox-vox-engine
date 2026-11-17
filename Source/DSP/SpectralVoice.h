#pragma once

#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

namespace epinox::dsp
{
// Mono phase-vocoder pitch shifter with an independent formant control.
//
// Pitch: bin remapping with phase accumulation (STFT, 2048 points, 75 % overlap).
// Formant: the spectral envelope is estimated by cepstral smoothing and re-imposed after the
// pitch shift. With formant = 0 the formants stay where they were (natural sounding pitch
// shift); a non-zero formant value moves the resonances by that many semitones.
// Fixed latency: SpectralVoice::latency samples.
class SpectralVoice
{
public:
    static constexpr int fftOrder = 11;
    static constexpr int fftSize  = 1 << fftOrder;
    static constexpr int overlap  = 4;
    static constexpr int hop      = fftSize / overlap;
    static constexpr int latency  = fftSize;          // reported delay: a sample leaves one full window after it enters
    static constexpr int fifoOffset = fftSize - hop;   // internal FIFO bookkeeping
    static constexpr int half     = fftSize / 2;

    SpectralVoice() : fft (fftOrder)
    {
        for (int i = 0; i < fftSize; ++i)
            window[static_cast<size_t> (i)] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi
                                                                      * static_cast<float> (i) / static_cast<float> (fftSize));
        reset();
    }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        lifter = juce::jlimit (8, 200, static_cast<int> (std::round (sr * 0.0012)));
        reset();
    }

    void reset()
    {
        inFifo.fill (0.0f);
        outFifo.fill (0.0f);
        outAccum.fill (0.0f);
        lastPhase.fill (0.0);
        sumPhase.fill (0.0);
        rover = fifoOffset;
        pitchRatio = formantRatio = targetPitch = targetFormant = 1.0f;
    }

    void setShift (float pitchSemitones, float formantSemitones)
    {
        targetPitch   = std::pow (2.0f, pitchSemitones / 12.0f);
        targetFormant = std::pow (2.0f, formantSemitones / 12.0f);
    }

    float processSample (float in)
    {
        inFifo[static_cast<size_t> (rover)] = in;
        const float out = outFifo[static_cast<size_t> (rover - fifoOffset)];
        ++rover;

        if (rover >= fftSize)
        {
            rover = fifoOffset;
            processFrame();
        }

        return out;
    }

private:
    // Output scaling that makes analysis + synthesis unity gain (verified by the unit tests).
    static constexpr float synthScale = 1.0f / 3.0f;

    void processFrame()
    {
        pitchRatio   += 0.5f * (targetPitch - pitchRatio);
        formantRatio += 0.5f * (targetFormant - formantRatio);

        for (int k = 0; k < fftSize; ++k)
            work[static_cast<size_t> (k)] = inFifo[static_cast<size_t> (k)] * window[static_cast<size_t> (k)];

        std::fill (work.begin() + fftSize, work.end(), 0.0f);
        fft.performRealOnlyForwardTransform (work.data(), true);

        const double pi = juce::MathConstants<double>::pi;
        const double freqPerBin = sr / fftSize;
        const double expct = 2.0 * pi * hop / fftSize;

        for (int k = 0; k <= half; ++k)
        {
            const double re = work[static_cast<size_t> (2 * k)];
            const double im = work[static_cast<size_t> (2 * k + 1)];
            const double magn = 2.0 * std::sqrt (re * re + im * im);
            const double phase = std::atan2 (im, re);

            double tmp = phase - lastPhase[static_cast<size_t> (k)];
            lastPhase[static_cast<size_t> (k)] = phase;
            tmp -= k * expct;

            int qpd = static_cast<int> (tmp / pi);
            qpd += (qpd >= 0) ? (qpd & 1) : -(qpd & 1);
            tmp -= pi * qpd;

            tmp = overlap * tmp / (2.0 * pi);
            tmp = k * freqPerBin + tmp * freqPerBin;

            anaMagn[static_cast<size_t> (k)] = static_cast<float> (magn);
            anaFreq[static_cast<size_t> (k)] = static_cast<float> (tmp);
        }

        const bool shifting = std::abs (pitchRatio - 1.0f) > 1.0e-4f || std::abs (formantRatio - 1.0f) > 1.0e-4f;

        if (! shifting)
        {
            synMagn = anaMagn;
            synFreq = anaFreq;
        }
        else
        {
            synMagn.fill (0.0f);
            synFreq.fill (0.0f);

            for (int k = 0; k <= half; ++k)
            {
                const int index = static_cast<int> (static_cast<float> (k) * pitchRatio);

                if (index <= half)
                {
                    synMagn[static_cast<size_t> (index)] += anaMagn[static_cast<size_t> (k)];
                    synFreq[static_cast<size_t> (index)] = anaFreq[static_cast<size_t> (k)] * pitchRatio;
                }
            }

            computeEnvelope();

            for (int k = 0; k <= half; ++k)
            {
                const float kf = static_cast<float> (k);
                const float gain = envAt (kf / formantRatio) / envAt (kf / pitchRatio);
                synMagn[static_cast<size_t> (k)] *= juce::jlimit (0.05f, 12.0f, gain);
            }
        }

        for (int k = 0; k <= half; ++k)
        {
            const double magn = synMagn[static_cast<size_t> (k)];
            double tmp = synFreq[static_cast<size_t> (k)];

            tmp -= k * freqPerBin;
            tmp /= freqPerBin;
            tmp = 2.0 * pi * tmp / overlap;
            tmp += k * expct;

            sumPhase[static_cast<size_t> (k)] += tmp;
            const double phase = sumPhase[static_cast<size_t> (k)];

            work[static_cast<size_t> (2 * k)] = static_cast<float> (magn * std::cos (phase));
            work[static_cast<size_t> (2 * k + 1)] = static_cast<float> (magn * std::sin (phase));
        }

        fft.performRealOnlyInverseTransform (work.data());

        for (int k = 0; k < fftSize; ++k)
            outAccum[static_cast<size_t> (k)] += window[static_cast<size_t> (k)] * work[static_cast<size_t> (k)] * synthScale;

        for (int k = 0; k < hop; ++k)
            outFifo[static_cast<size_t> (k)] = outAccum[static_cast<size_t> (k)];

        std::move (outAccum.begin() + hop, outAccum.end(), outAccum.begin());
        std::fill (outAccum.end() - hop, outAccum.end(), 0.0f);

        std::move (inFifo.begin() + hop, inFifo.end(), inFifo.begin());
    }

    // Cepstral smoothing of the log magnitude spectrum.
    void computeEnvelope()
    {
        for (int k = 0; k <= half; ++k)
        {
            const float l = std::log (std::max (anaMagn[static_cast<size_t> (k)], 1.0e-6f));
            cep[static_cast<size_t> (k)] = { l, 0.0f };

            if (k > 0 && k < half)
                cep[static_cast<size_t> (fftSize - k)] = { l, 0.0f };
        }

        fft.perform (cep.data(), cepOut.data(), true);

        for (int n = lifter + 1; n < fftSize - lifter; ++n)
            cepOut[static_cast<size_t> (n)] = { 0.0f, 0.0f };

        fft.perform (cepOut.data(), cep.data(), false);

        for (int k = 0; k <= half; ++k)
            env[static_cast<size_t> (k)] = std::exp (cep[static_cast<size_t> (k)].real());
    }

    float envAt (float x) const
    {
        if (x <= 0.0f)
            return env[0];

        if (x >= static_cast<float> (half))
            return env[static_cast<size_t> (half)];

        const int i = static_cast<int> (x);
        const float f = x - static_cast<float> (i);
        return env[static_cast<size_t> (i)] * (1.0f - f) + env[static_cast<size_t> (i + 1)] * f;
    }

    juce::dsp::FFT fft;
    double sr = 44100.0;
    int lifter = 53;
    int rover = fifoOffset;
    float pitchRatio = 1.0f, formantRatio = 1.0f, targetPitch = 1.0f, targetFormant = 1.0f;

    std::array<float, fftSize> window {}, inFifo {}, outAccum {};
    std::array<float, hop> outFifo {};
    std::array<float, 2 * fftSize> work {};
    std::array<double, half + 1> lastPhase {}, sumPhase {};
    std::array<float, half + 1> anaMagn {}, anaFreq {}, synMagn {}, synFreq {}, env {};
    std::array<std::complex<float>, fftSize> cep {}, cepOut {};
};
} // namespace epinox::dsp
