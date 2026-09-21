#include "cst_audio/audio.hpp"

#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace cst_audio;
#define CHECK(condition) do { if (!(condition)) { \
  std::cerr << "CHECK FAILED: " #condition << " at " << __FILE__ << ':' << __LINE__ << "\n"; \
  return 1; } } while (false)
int main() {
  State12 zero{};
  const auto state = update_dyn12(zero, {0.5, -0.25}, 0);
  for (std::size_t i = 0; i < 12; ++i) {
    const double u = i % 2 == 0 ? 0.5 : -0.25;
    const double expected = std::tanh(0.14 * u + 0.015 * std::sin((i + 1) * 0.17320508075688773));
    CHECK(std::abs(state[i] - expected) < 1e-13);
  }
  auto adapted = adapt_config({2, 1ull << 30, "test"});
  CHECK(adapted.sample_rate == 32000 && adapted.block_frames == 512);
  CHECK(adapt_config({8, 16ull << 30, "test"}).sample_rate == 48000);
  Config cfg; cfg.sample_rate = 24000; cfg.channels = 2; cfg.block_frames = 128; cfg.ambience = false;
  std::vector<Event> sequence{{0.05, Surface::wood, 0.8, -0.3}, {0.3, Surface::metal, 0.9, 0.2}};
  const auto a = render(cfg, 1.0, sequence);
  const auto b = render(cfg, 1.0, sequence);
  CHECK(a.frames == 24000 && a.interleaved == b.interleaved);
  CHECK(a.peak > 0.01 && a.peak < 1 && a.rms > 0.001);
  cfg.dynamic_state = false;
  const auto control = render(cfg, 1.0, sequence);
  CHECK(a.interleaved != control.interleaved);
  CHECK(control.final_state == zero);
  bool failed = false;
  try { auto invalid = cfg; invalid.channels = 3; (void)render(invalid, 1, sequence); }
  catch (const std::invalid_argument&) { failed = true; }
  CHECK(failed);
  failed = false;
  try { (void)render(cfg, -1.0, sequence); }
  catch (const std::invalid_argument&) { failed = true; }
  CHECK(failed);
  cfg.dynamic_state = true;
  const std::string wav = "cst_audio_test.wav";
  write_wav16(wav, cfg, a);
  std::ifstream in(wav, std::ios::binary | std::ios::ate);
  CHECK(in && in.tellg() == static_cast<std::streamoff>(44 + a.frames * 4));
  in.seekg(0); std::string riff(4, '\0'); in.read(&riff[0], 4); CHECK(riff == "RIFF");
  in.close(); std::remove(wav.c_str());
  std::cout << "CST_AUDIO_TESTS=PASS deterministic WAV, state, static control, adaptation, validation\n";
}
