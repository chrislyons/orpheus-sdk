// SPDX-License-Identifier: MIT
// Temporary hosted MSVC probe. Delete with windows-rt-diagnostic.yml after the gate is resolved.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>
#define NOMINMAX
#include "forced_true_peak_meter.h"
#include "routing/gain_smoother.h"
#include "routing/routing_matrix.h"
#include <windows.h>

#if defined(_M_X64)
namespace {
using orpheus::ForcedTruePeakMeter;
using orpheus::TruePeakMeter;

__declspec(noinline) float current_noinline(TruePeakMeter& m, float x) {
  return m.process(x);
}
__forceinline float current_wrapper_forceinline(TruePeakMeter& m, float x) {
  return m.process(x);
}
__forceinline float forced_inline(ForcedTruePeakMeter& m, float x) {
  return m.process(x);
}

// Genuine phase-lane SSE2 candidate using the production 4x12 coefficient table.
class PhaseLaneCandidate {
public:
  void reset() noexcept {
    history_.fill(0.0f);
    head_ = 0;
  }
  __forceinline float process(float sample) noexcept {
    head_ = head_ == 0 ? 11u : head_ - 1u;
    history_[head_] = sample;
    history_[head_ + 12] = sample;
    const float* h = history_.data() + head_;
    __m128 sum = _mm_setzero_ps();
    for (unsigned tap = 0; tap < 12; ++tap)
      sum = _mm_add_ps(sum, _mm_mul_ps(_mm_set1_ps(h[tap]),
                                       _mm_setr_ps(ForcedTruePeakMeter::s_filterCoeffs[0][tap],
                                                   ForcedTruePeakMeter::s_filterCoeffs[1][tap],
                                                   ForcedTruePeakMeter::s_filterCoeffs[2][tap],
                                                   ForcedTruePeakMeter::s_filterCoeffs[3][tap])));
    float peak = std::fabs(sample);
    peak = (std::max)(peak, std::fabs(_mm_cvtss_f32(sum)));
    peak = (std::max)(peak, std::fabs(_mm_cvtss_f32(_mm_shuffle_ps(sum, sum, 1))));
    peak = (std::max)(peak, std::fabs(_mm_cvtss_f32(_mm_shuffle_ps(sum, sum, 2))));
    peak = (std::max)(peak, std::fabs(_mm_cvtss_f32(_mm_shuffle_ps(sum, sum, 3))));
    return peak;
  }

private:
  std::array<float, 24> history_{};
  unsigned head_{0};
};
class ScalarOracle {
public:
  void reset() noexcept {
    history_.fill(0.0f);
    head_ = 0;
  }
  float process(float sample) noexcept {
    head_ = head_ == 0 ? 11u : head_ - 1u;
    history_[head_] = sample;
    history_[head_ + 12] = sample;
    const float* h = history_.data() + head_;
    float peak = std::fabs(sample);
    for (unsigned phase = 0; phase < 4; ++phase) {
      float sum = 0.0f;
      for (unsigned tap = 0; tap < 12; ++tap)
        sum += h[tap] * ForcedTruePeakMeter::s_filterCoeffs[phase][tap];
      peak = (std::max)(peak, std::fabs(sum));
    }
    return peak;
  }

private:
  std::array<float, 24> history_{};
  unsigned head_{0};
};
__forceinline float candidate_forceinline(PhaseLaneCandidate& m, float x) {
  return m.process(x);
}
__declspec(noinline) float candidate_noinline(PhaseLaneCandidate& m, float x) {
  return m.process(x);
}

struct Timing {
  double ns_per_sample;
  float checksum;
};
template <class Meter, float (*Step)(Meter&, float)>
Timing bench_meter(const std::vector<float>& input) {
  Meter meter;
  float checksum = 0.0f;
  LARGE_INTEGER f{}, a{}, b{};
  QueryPerformanceFrequency(&f);
  QueryPerformanceCounter(&a);
  for (unsigned r = 0; r < 8; ++r)
    for (float x : input)
      checksum += Step(meter, x);
  QueryPerformanceCounter(&b);
  return {1e9 * (double)(b.QuadPart - a.QuadPart) / ((double)f.QuadPart * input.size() * 8),
          checksum};
}

struct RoutingTiming {
  double us;
  float checksum;
};
RoutingTiming bench_routing(orpheus::MeteringMode mode, bool meters, const char* label) {
  constexpr unsigned channels = 256, groups = 32, outputs = 32, frames = 512, callbacks = 300;
  orpheus::RoutingConfig cfg;
  cfg.num_channels = channels;
  cfg.num_groups = groups;
  cfg.num_outputs = outputs;
  cfg.sample_rate = 48000;
  cfg.gain_smoothing_ms = 0.0f;
  cfg.enable_metering = meters;
  cfg.enable_clipping_protection = false;
  cfg.source_channel_policy = orpheus::SourceChannelPolicy::Discrete;
  cfg.metering_mode = mode;
  auto matrix = std::make_unique<orpheus::RoutingMatrix>();
  if (matrix->initialize(cfg) != orpheus::SessionGraphError::OK)
    std::abort();
  std::vector<std::vector<float>> in(channels, std::vector<float>(frames, 0.01f));
  std::vector<const float*> ip;
  for (auto& x : in)
    ip.push_back(x.data());
  std::vector<std::vector<float>> out(outputs, std::vector<float>(frames));
  std::vector<float*> op;
  for (auto& x : out)
    op.push_back(x.data());
  for (unsigned c = 0; c < channels; ++c)
    if (matrix->setChannelRoute(c, c / 8, c % 8) != orpheus::SessionGraphError::OK)
      std::abort();
  for (int i = 0; i < 4; ++i)
    if (matrix->processRouting(ip.data(), op.data(), frames) != orpheus::SessionGraphError::OK)
      std::abort();
  LARGE_INTEGER f{}, a{}, b{};
  QueryPerformanceFrequency(&f);
  QueryPerformanceCounter(&a);
  float checksum = 0;
  for (unsigned i = 0; i < callbacks; ++i) {
    if (matrix->processRouting(ip.data(), op.data(), frames) != orpheus::SessionGraphError::OK)
      std::abort();
    checksum += op[0][i % frames];
  }
  QueryPerformanceCounter(&b);
  double us = 1e6 * (double)(b.QuadPart - a.QuadPart) / ((double)f.QuadPart * callbacks);
  std::printf("routing %s us_per_callback=%.3f budget_us=%.3f checksum=%.9g\n", label, us,
              frames * 1e6 / 48000.0, checksum);
  return {us, checksum};
}

bool compare() {
  std::vector<float> samples(4096);
  for (size_t i = 0; i < samples.size(); ++i)
    samples[i] = std::sin(float(i) * .017f) * .8f + (int(i % 37) - 18) * .0007f;
  TruePeakMeter a;
  ForcedTruePeakMeter b;
  PhaseLaneCandidate c;
  ScalarOracle oracle;
  float ef = 0, ec = 0, eo = 0;
  for (float x : samples) {
    float av = a.process(x);
    float ov = oracle.process(x);
    ef = (std::max)(ef, std::fabs(av - b.process(x)));
    ec = (std::max)(ec, std::fabs(av - c.process(x)));
    eo = (std::max)(eo, std::fabs(av - ov));
  }
  a.reset();
  b.reset();
  c.reset();
  oracle.reset();
  for (size_t i = 0; i < 96; ++i) {
    float x = i == 0 ? 1.f : 0.f;
    float av = a.process(x);
    ef = (std::max)(ef, std::fabs(av - b.process(x)));
    ec = (std::max)(ec, std::fabs(av - c.process(x)));
    eo = (std::max)(eo, std::fabs(av - oracle.process(x)));
  }
  std::printf(
      "compare scalar_oracle_max_abs_error=%.9g finite_samples=4096 impulse_trailing_zeros=95 "
      "forced_inline_max_abs_error=%.9g phase_lane_sse2_max_abs_error=%.9g\n",
      eo, ef, ec);
  return eo <= 1e-6f && ef <= 1e-6f && ec <= 1e-6f;
}
} // namespace
int main() {
  if (!compare()) {
    std::fprintf(stderr, "numerical comparison failed\n");
    return 2;
  }
  std::vector<float> input(1u << 20);
  for (size_t i = 0; i < input.size(); ++i)
    input[i] = std::sin(float(i) * .013f) * .5f;
  auto a = bench_meter<TruePeakMeter, current_noinline>(input),
       b = bench_meter<TruePeakMeter, current_wrapper_forceinline>(input);
  auto f = bench_meter<ForcedTruePeakMeter, forced_inline>(input);
  auto c = bench_meter<PhaseLaneCandidate, candidate_noinline>(input),
       d = bench_meter<PhaseLaneCandidate, candidate_forceinline>(input);
  std::printf("meter current_noinline ns_per_sample=%.3f checksum=%.9g\n", a.ns_per_sample,
              a.checksum);
  std::printf("meter current_wrapper_forceinline ns_per_sample=%.3f checksum=%.9g\n",
              b.ns_per_sample, b.checksum);
  std::printf("meter forced_production_copy_forceinline ns_per_sample=%.3f checksum=%.9g\n",
              f.ns_per_sample, f.checksum);
  std::printf("meter candidate_phase_lane_noinline ns_per_sample=%.3f checksum=%.9g\n",
              c.ns_per_sample, c.checksum);
  std::printf("meter candidate_phase_lane_forceinline ns_per_sample=%.3f checksum=%.9g\n",
              d.ns_per_sample, d.checksum);
  bench_routing(orpheus::MeteringMode::Peak, true, "sample-peak");
  bench_routing(orpheus::MeteringMode::TruePeak, true, "true-peak");
  bench_routing(orpheus::MeteringMode::Peak, false, "no-meters");
  return 0;
}
#else
int main() {
  std::puts("windows_rt_diagnostic requires MSVC x64");
  return 0;
}
#endif
