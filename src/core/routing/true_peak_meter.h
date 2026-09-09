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
    const __m128 history0 = _mm_loadu_ps(history);
    const __m128 history7 = _mm_loadu_ps(history + 7);
    const __m128 history8 = _mm_loadu_ps(history + 8);
    const __m128 pair02 =
        _mm_add_ps(history0, _mm_shuffle_ps(history7, history7, _MM_SHUFFLE(0, 1, 2, 3)));
    const auto horizontalSum = [](__m128 value) noexcept {
      __m128 high = _mm_movehl_ps(value, value);
      value = _mm_add_ps(value, high);
      high = _mm_shuffle_ps(value, value, 1);
      value = _mm_add_ss(value, high);
      return _mm_cvtss_f32(value);
    };

    const auto& phase0 = s_filterCoeffs[0];
    float interpolated = horizontalSum(_mm_mul_ps(pair02, _mm_loadu_ps(phase0.data()))) +
                         (history[4] + history[6]) * phase0[4] + history[5] * phase0[5];
    peak = std::max(peak, std::abs(interpolated));

    const __m128 pair1Sum =
        _mm_add_ps(history0, _mm_shuffle_ps(history8, history8, _MM_SHUFFLE(0, 1, 2, 3)));
    const __m128 pair1Difference =
        _mm_sub_ps(history0, _mm_shuffle_ps(history8, history8, _MM_SHUFFLE(0, 1, 2, 3)));
    const __m128 phase1PairSums =
        _mm_setr_ps(0.00030517578125f, -0.00299072265625f, -0.03240966796875f, 0.001373291015625f);
    const __m128 phase1PairDifferences =
        _mm_setr_ps(0.00274658203125f, -0.01031494140625f, -0.01580810546875f, 0.046234130859375f);
    float phase1Even = horizontalSum(_mm_mul_ps(pair1Sum, phase1PairSums));
    float phase1Odd = horizontalSum(_mm_mul_ps(pair1Difference, phase1PairDifferences));
    phase1Even += (history[4] + history[7]) * 0.1697998046875f +
                  (history[5] + history[6]) * 0.33294677734375f;
    phase1Odd += (history[4] - history[7]) * 0.1221923828125f +
                 (history[5] - history[6]) * 0.11090087890625f;
    peak = std::max(peak, std::abs(phase1Even + phase1Odd));

    const auto& phase2 = s_filterCoeffs[2];
    interpolated = horizontalSum(_mm_mul_ps(pair02, _mm_loadu_ps(phase2.data()))) +
                   (history[4] + history[6]) * phase2[4] + history[5] * phase2[5];
    peak = std::max(peak, std::abs(interpolated));
    peak = std::max(peak, std::abs(phase1Even - phase1Odd));
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
