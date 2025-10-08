#include "orpheus/dsp/oscillator.hpp"

#include <algorithm>
#include <cmath>
#include <random>

namespace orpheus::dsp {
namespace {
constexpr double kMinSampleRate = 1.0;
constexpr double kMinFrequency = 0.01;
constexpr double kMaxPulseWidth = 0.995;
constexpr double kMinPulseWidth = 0.005;

class RandomEngine {
 public:
  RandomEngine() : engine_(std::random_device{}()), distribution_(-1.0, 1.0) {}

  [[nodiscard]] double white() noexcept { return distribution_(engine_); }

 private:
  std::mt19937 engine_;
  std::uniform_real_distribution<double> distribution_;
};

RandomEngine& random_engine() {
  thread_local RandomEngine engine;
  return engine;
}

}  // namespace

Oscillator::Oscillator() = default;

Oscillator::Oscillator(double sample_rate) { set_sample_rate(sample_rate); }

void Oscillator::set_sample_rate(double sample_rate) noexcept {
  sample_rate_.store(std::max(sample_rate, kMinSampleRate));
}

double Oscillator::sample_rate() const noexcept { return sample_rate_.load(); }

void Oscillator::set_frequency(double frequency_hz) noexcept {
  frequency_.store(std::max(frequency_hz, kMinFrequency));
}

double Oscillator::frequency() const noexcept { return frequency_.load(); }

void Oscillator::set_waveform(Waveform waveform) noexcept { waveform_.store(waveform); }

Waveform Oscillator::waveform() const noexcept { return waveform_.load(); }

void Oscillator::set_phase(double phase) noexcept {
  const double wrapped = wrap_phase(phase);
  for (auto& voice : voices_) {
    voice.phase = wrapped;
    voice.integrator = 0.0;
    voice.sub_phase = wrapped * 0.5;
  }
}

void Oscillator::reset_phase() noexcept { set_phase(0.0); }

double Oscillator::phase(std::size_t voice) const noexcept {
  return voices_.at(std::min<std::size_t>(voice, kMaxVoices - 1)).phase;
}

void Oscillator::set_pulse_width(double width) noexcept {
  const double clamped = clamp(width, kMinPulseWidth, kMaxPulseWidth);
  pulse_width_.store(clamped);
}

double Oscillator::pulse_width() const noexcept { return pulse_width_.load(); }

void Oscillator::set_unison_voice_count(std::size_t voices) noexcept {
  voice_count_.store(std::clamp<std::size_t>(voices, 1, kMaxVoices));
}

std::size_t Oscillator::unison_voice_count() const noexcept { return voice_count_.load(); }

void Oscillator::set_unison_detune_cents(double cents) noexcept {
  detune_cents_.store(std::clamp(cents, 0.0, 1200.0));
}

double Oscillator::unison_detune_cents() const noexcept { return detune_cents_.load(); }

void Oscillator::enable_sub_oscillator(bool enabled) noexcept { sub_oscillator_.store(enabled); }

bool Oscillator::sub_oscillator_enabled() const noexcept { return sub_oscillator_.load(); }

void Oscillator::set_lfo_mode(bool enabled) noexcept { lfo_mode_.store(enabled); }

bool Oscillator::lfo_mode() const noexcept { return lfo_mode_.load(); }

void Oscillator::set_frequency_modulation_depth(double depth_ratio) noexcept {
  fm_depth_.store(std::max(0.0, depth_ratio));
}

double Oscillator::frequency_modulation_depth() const noexcept { return fm_depth_.load(); }

float Oscillator::process(float fm_input) noexcept {
  const auto sr = std::max(sample_rate_.load(std::memory_order_relaxed), kMinSampleRate);
  const auto base_freq = std::max(frequency_.load(std::memory_order_relaxed), kMinFrequency);
  const auto voices = voice_count_.load(std::memory_order_relaxed);
  const auto pw = pulse_width_.load(std::memory_order_relaxed);
  const auto fm_depth = fm_depth_.load(std::memory_order_relaxed);
  const auto sub_enabled = sub_oscillator_.load(std::memory_order_relaxed);

  const double lfo_scale = lfo_mode_.load(std::memory_order_relaxed) ? 0.5 : 1.0;

  const double fm_ratio = std::clamp(static_cast<double>(fm_input), -1.0, 1.0) * fm_depth;
  const double fm_hz = base_freq * fm_ratio;

  double sample_accumulator = 0.0;
  double sub_accumulator = 0.0;
  for (std::size_t v = 0; v < voices; ++v) {
    const double detune = voice_detune(v);
    const double freq = (base_freq + fm_hz) * detune;
    const double scaled_freq = std::max(freq, kMinFrequency) * lfo_scale;
    const double phase_increment = scaled_freq / sr;
    sample_accumulator +=
        process_voice(v, scaled_freq, phase_increment, pw, sr, sub_accumulator);
  }

  double sample = sample_accumulator / static_cast<double>(voices);
  if (sub_enabled) {
    const double sub_sample = sub_accumulator / static_cast<double>(voices);
    sample = (sample + sub_sample) * 0.5;
  }

  return static_cast<float>(std::clamp(sample, -1.0, 1.0));
}

void Oscillator::process(std::span<float> output, float fm_input) noexcept {
  for (auto& sample : output) {
    sample = process(fm_input);
  }
}

double Oscillator::process_voice(std::size_t voice_index, double frequency_hz,
                                 double phase_increment, double pulse_width,
                                 double sample_rate, double& sub_mix) noexcept {
  auto& voice = voices_[voice_index];
  voice.phase = wrap_phase(voice.phase + phase_increment);

  const auto waveform = waveform_.load(std::memory_order_relaxed);
  double sample = 0.0;

  switch (waveform) {
    case Waveform::Sine: {
      sample = sine_from_table(voice.phase);
      break;
    }
    case Waveform::Saw: {
      const double naive = 2.0 * voice.phase - 1.0;
      const double blep = poly_blep(voice.phase, phase_increment);
      sample = naive - blep;
      break;
    }
    case Waveform::Square: {
      const double duty = 0.5;
      double square = voice.phase < duty ? 1.0 : -1.0;
      square += poly_blep(voice.phase, phase_increment);
      double t = voice.phase + (1.0 - duty);
      if (t >= 1.0) {
        t -= 1.0;
      }
      square -= poly_blep(t, phase_increment);
      sample = square;
      break;
    }
    case Waveform::Pulse: {
      const double duty = pulse_width;
      double pulse = voice.phase < duty ? 1.0 : -1.0;
      pulse += poly_blep(voice.phase, phase_increment);
      double t = voice.phase + (1.0 - duty);
      if (t >= 1.0) {
        t -= 1.0;
      }
      pulse -= poly_blep(t, phase_increment);
      sample = pulse;
      break;
    }
    case Waveform::Triangle: {
      double square = voice.phase < 0.5 ? 1.0 : -1.0;
      square += poly_blep(voice.phase, phase_increment);
      double t = voice.phase + 0.5;
      if (t >= 1.0) {
        t -= 1.0;
      }
      square -= poly_blep(t, phase_increment);
      voice.integrator += square * phase_increment;
      voice.integrator -= voice.integrator * 0.001;
      sample = voice.integrator * 4.0;
      break;
    }
    case Waveform::WhiteNoise: {
      sample = random_engine().white();
      break;
    }
    case Waveform::PinkNoise: {
      const double white = random_engine().white();
      auto& state = voice.pink;
      state.b0 = 0.99765 * state.b0 + white * 0.0990460;
      state.b1 = 0.96300 * state.b1 + white * 0.2965164;
      state.b2 = 0.57000 * state.b2 + white * 1.0526913;
      const double pink = state.b0 + state.b1 + state.b2 + state.b3 + white * 0.1848;
      state.b3 = white * 0.5362;
      sample = pink * 0.05;
      break;
    }
  }

  const double sub_increment = std::max(frequency_hz * 0.5 / sample_rate, 0.0);
  voice.sub_phase = wrap_phase(voice.sub_phase + sub_increment);
  sub_mix += sine_from_table(voice.sub_phase);

  const double dc_alpha = 0.001;
  voice.dc += dc_alpha * (sample - voice.dc);
  sample -= voice.dc;

  return sample;
}

double Oscillator::poly_blep(double t, double dt) noexcept {
  if (dt <= 0.0) {
    return 0.0;
  }

  if (t < dt) {
    t /= dt;
    return t + t - t * t - 1.0;
  }

  if (t > 1.0 - dt) {
    t = (t - 1.0) / dt;
    return t * t + t + t + 1.0;
  }

  return 0.0;
}

double Oscillator::wrap_phase(double phase) noexcept {
  double wrapped = phase - std::floor(phase);
  if (wrapped < 0.0) {
    wrapped += 1.0;
  }
  return wrapped;
}

double Oscillator::clamp(double value, double min, double max) noexcept {
  return std::max(min, std::min(value, max));
}

double Oscillator::voice_detune(std::size_t voice_index) const noexcept {
  const auto voices = voice_count_.load(std::memory_order_relaxed);
  if (voices <= 1) {
    return 1.0;
  }
  const double spread = detune_cents_.load(std::memory_order_relaxed);
  if (spread <= 0.0) {
    return 1.0;
  }

  const double cents_per_voice = spread / static_cast<double>(voices - 1);
  const double offset_cents = (static_cast<double>(voice_index) * cents_per_voice) - spread / 2.0;
  return std::pow(2.0, offset_cents / 1200.0);
}

double Oscillator::sine_from_table(double phase) noexcept {
  const double wrapped_phase = wrap_phase(phase);
  const double index = wrapped_phase * static_cast<double>(kSineTableSize);
  const auto base_index = static_cast<std::size_t>(index) % kSineTableSize;
  const auto next_index = (base_index + 1) % kSineTableSize;
  const double frac = index - static_cast<double>(base_index);
  return lerp(kSineTable[base_index], kSineTable[next_index], frac);
}

}  // namespace orpheus::dsp

