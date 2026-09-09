// SPDX-License-Identifier: MIT
// ORP121 Q-04: True-Peak Metering Estimator
#pragma once

#include <array>
#include <cmath>
#if defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace orpheus {

/// SDK true-peak estimator modeled against ITU-R BS.1770-4 terminology.
///
/// This estimator uses 4x oversampling to expose inter-sample peaks that can
/// cause clipping in downstream D/A converters. It is not a standalone claim
/// of standards conformance.
///
/// Configuration:
/// - 4x oversampling factor
/// - 12-tap polyphase filter per phase (48 total coefficients)
/// - Zero allocation and bounded work per input sample
class TruePeakMeter {
public:
  static constexpr int OVERSAMPLE_FACTOR = 4;
  static constexpr int TAPS_PER_PHASE = 12;
  static constexpr int TOTAL_TAPS = OVERSAMPLE_FACTOR * TAPS_PER_PHASE; // 48

  TruePeakMeter() {
    reset();
  }

  /// Reset filter history
  void reset() {
    m_history.fill(0.0f);
    m_history_head = 0;
  }

  /// Process a single sample and return true-peak value
  /// @param sample Input sample
  /// @return Maximum interpolated peak value (absolute)
  float process(float sample) {
    // A duplicated circular history keeps the newest-to-oldest tap order
    // contiguous without shifting eleven samples for every input sample.
    m_history_head =
        m_history_head == 0 ? static_cast<size_t>(TAPS_PER_PHASE - 1) : m_history_head - 1;
    m_history[m_history_head] = sample;
    m_history[m_history_head + TAPS_PER_PHASE] = sample;

    float peak = std::abs(sample); // Include original sample
    const float* history = &m_history[m_history_head];
#if defined(_M_X64)
    // The four FIR phases have symmetry: phases 0 and 2 mirror around tap 5,
    // while phase 3 is phase 1 in reverse order. Pairing those taps cuts the
    // Windows x64 hot path from 48 to 24 multiplies without changing the
    // estimator's coefficient set, history, or reset behavior.
    const float h0 = history[0];
    const float h1 = history[1];
    const float h2 = history[2];
    const float h3 = history[3];
    const float h4 = history[4];
    const float h5 = history[5];
    const float h6 = history[6];
    const float h7 = history[7];
    const float h8 = history[8];
    const float h9 = history[9];
    const float h10 = history[10];
    const float h11 = history[11];
    const float h0PlusH10 = h0 + h10;
    const float h1PlusH9 = h1 + h9;
    const float h2PlusH8 = h2 + h8;
    const float h3PlusH7 = h3 + h7;
    const float h4PlusH6 = h4 + h6;
    const auto& phase0 = s_filterCoeffs[0];
    const auto& phase2 = s_filterCoeffs[2];
    float interpolated = h5 * phase0[5];
    interpolated += h0PlusH10 * phase0[0];
    interpolated += h1PlusH9 * phase0[1];
    interpolated += h2PlusH8 * phase0[2];
    interpolated += h3PlusH7 * phase0[3];
    interpolated += h4PlusH6 * phase0[4];
    peak = std::max(peak, std::abs(interpolated));

    interpolated = h5 * phase2[5];
    interpolated += h0PlusH10 * phase2[0];
    interpolated += h1PlusH9 * phase2[1];
    interpolated += h2PlusH8 * phase2[2];
    interpolated += h3PlusH7 * phase2[3];
    interpolated += h4PlusH6 * phase2[4];
    peak = std::max(peak, std::abs(interpolated));

    const auto& phase1 = s_filterCoeffs[1];
    const float h0PlusH11 = h0 + h11;
    const float h1PlusH10 = h1 + h10;
    const float h2PlusH9 = h2 + h9;
    const float h3PlusH8 = h3 + h8;
    const float h4PlusH7 = h4 + h7;
    const float h5PlusH6 = h5 + h6;
    const float h0MinusH11 = h0 - h11;
    const float h1MinusH10 = h1 - h10;
    const float h2MinusH9 = h2 - h9;
    const float h3MinusH8 = h3 - h8;
    const float h4MinusH7 = h4 - h7;
    const float h5MinusH6 = h5 - h6;
    float phase1Numerator =
        h0PlusH11 * (phase1[0] + phase1[11]) + h0MinusH11 * (phase1[0] - phase1[11]);
    float phase3Numerator =
        h0PlusH11 * (phase1[0] + phase1[11]) - h0MinusH11 * (phase1[0] - phase1[11]);
    phase1Numerator += h1PlusH10 * (phase1[1] + phase1[10]) + h1MinusH10 * (phase1[1] - phase1[10]);
    phase3Numerator += h1PlusH10 * (phase1[1] + phase1[10]) - h1MinusH10 * (phase1[1] - phase1[10]);
    phase1Numerator += h2PlusH9 * (phase1[2] + phase1[9]) + h2MinusH9 * (phase1[2] - phase1[9]);
    phase3Numerator += h2PlusH9 * (phase1[2] + phase1[9]) - h2MinusH9 * (phase1[2] - phase1[9]);
    phase1Numerator += h3PlusH8 * (phase1[3] + phase1[8]) + h3MinusH8 * (phase1[3] - phase1[8]);
    phase3Numerator += h3PlusH8 * (phase1[3] + phase1[8]) - h3MinusH8 * (phase1[3] - phase1[8]);
    phase1Numerator += h4PlusH7 * (phase1[4] + phase1[7]) + h4MinusH7 * (phase1[4] - phase1[7]);
    phase3Numerator += h4PlusH7 * (phase1[4] + phase1[7]) - h4MinusH7 * (phase1[4] - phase1[7]);
    phase1Numerator += h5PlusH6 * (phase1[5] + phase1[6]) + h5MinusH6 * (phase1[5] - phase1[6]);
    phase3Numerator += h5PlusH6 * (phase1[5] + phase1[6]) - h5MinusH6 * (phase1[5] - phase1[6]);
    peak = std::max(peak, std::abs(0.5f * phase1Numerator));
    peak = std::max(peak, std::abs(0.5f * phase3Numerator));
#elif defined(__SSE2__)
    const __m128 history0 = _mm_loadu_ps(history);
    const __m128 history1 = _mm_loadu_ps(history + 4);
    const __m128 history2 = _mm_loadu_ps(history + 8);
    for (size_t phase = 0; phase < static_cast<size_t>(OVERSAMPLE_FACTOR); ++phase) {
      const auto& coefficients = s_filterCoeffs[phase];
      __m128 sum = _mm_mul_ps(history0, _mm_loadu_ps(coefficients.data()));
      sum = _mm_add_ps(sum, _mm_mul_ps(history1, _mm_loadu_ps(coefficients.data() + 4)));
      sum = _mm_add_ps(sum, _mm_mul_ps(history2, _mm_loadu_ps(coefficients.data() + 8)));
      __m128 high = _mm_movehl_ps(sum, sum);
      sum = _mm_add_ps(sum, high);
      high = _mm_shuffle_ps(sum, sum, 1);
      sum = _mm_add_ss(sum, high);
      const float interpolated = _mm_cvtss_f32(sum);
      peak = std::max(peak, std::abs(interpolated));
    }
#else
    for (size_t phase = 0; phase < static_cast<size_t>(OVERSAMPLE_FACTOR); ++phase) {
      const auto& coefficients = s_filterCoeffs[phase];
      float interpolated = history[0] * coefficients[0];
      interpolated += history[1] * coefficients[1];
      interpolated += history[2] * coefficients[2];
      interpolated += history[3] * coefficients[3];
      interpolated += history[4] * coefficients[4];
      interpolated += history[5] * coefficients[5];
      interpolated += history[6] * coefficients[6];
      interpolated += history[7] * coefficients[7];
      interpolated += history[8] * coefficients[8];
      interpolated += history[9] * coefficients[9];
      interpolated += history[10] * coefficients[10];
      interpolated += history[11] * coefficients[11];
      peak = std::max(peak, std::abs(interpolated));
    }
#endif

    return peak;
  }

  /// Process a buffer and return the maximum true-peak
  /// @param buffer Input buffer
  /// @param num_frames Number of samples
  /// @return Maximum true-peak value in the buffer
  float processBuffer(const float* buffer, size_t num_frames) {
    float max_peak = 0.0f;
    for (size_t i = 0; i < num_frames; ++i) {
      float peak = process(buffer[i]);
      max_peak = std::max(max_peak, peak);
    }
    return max_peak;
  }

private:
  std::array<float, TAPS_PER_PHASE * 2> m_history{};
  size_t m_history_head{0};

  // SDK polyphase FIR filter coefficients.
  // 4 phases × 12 taps = 48 coefficients.
  // These windowed-sinc coefficients are selected for the SDK's 4x estimator.
  //
  // Phase 0: Samples at original positions (identity + filtering)
  // Phase 1: Samples at 1/4 offset
  // Phase 2: Samples at 2/4 offset (midpoint)
  // Phase 3: Samples at 3/4 offset
  static constexpr std::array<std::array<float, TAPS_PER_PHASE>, OVERSAMPLE_FACTOR> s_filterCoeffs =
      {{
          // Phase 0 (original sample positions with anti-aliasing)
          {0.0017089843750f, -0.0291748046875f, -0.0189208984375f, 0.1109619140625f,
           0.2817382812500f, 0.3876953125000f, 0.2817382812500f, 0.1109619140625f,
           -0.0189208984375f, -0.0291748046875f, 0.0017089843750f, 0.0000000000000f},
          // Phase 1 (1/4 sample offset)
          {0.0030517578125f, -0.0133056640625f, -0.0482177734375f, 0.0476074218750f,
           0.2919921875000f, 0.4438476562500f, 0.2220458984375f, 0.0476074218750f,
           -0.0448608398438f, -0.0166015625000f, 0.0073242187500f, -0.0024414062500f},
          // Phase 2 (1/2 sample offset - midpoint)
          {0.0024414062500f, 0.0073242187500f, -0.0598144531250f, -0.0166015625000f,
           0.2324218750000f, 0.4638671875000f, 0.2324218750000f, -0.0166015625000f,
           -0.0598144531250f, 0.0073242187500f, 0.0024414062500f, 0.0000000000000f},
          // Phase 3 (3/4 sample offset)
          {-0.0024414062500f, 0.0073242187500f, -0.0166015625000f, -0.0448608398438f,
           0.0476074218750f, 0.2220458984375f, 0.4438476562500f, 0.2919921875000f, 0.0476074218750f,
           -0.0482177734375f, -0.0133056640625f, 0.0030517578125f},
      }};
};

// Static member definition
constexpr std::array<std::array<float, TruePeakMeter::TAPS_PER_PHASE>,
                     TruePeakMeter::OVERSAMPLE_FACTOR>
    TruePeakMeter::s_filterCoeffs;

} // namespace orpheus
