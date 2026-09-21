#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cst_audio {

// Matches the public Beast Box reference update, not a private canonical engine.
using State12 = std::array<double, 12>;
State12 update_dyn12(const State12& state, const std::vector<double>& drive, std::uint64_t step);

enum class Surface { wood, gravel, metal, concrete, fabric, water };
Surface parse_surface(const std::string& name);
const char* surface_name(Surface surface);

struct Hardware {
  unsigned logical_cores = 1;
  std::uint64_t physical_ram_bytes = 0; // Zero means unknown.
  std::string os;
};
Hardware probe_hardware();

struct Config {
  unsigned sample_rate = 48000;
  unsigned channels = 2;
  unsigned block_frames = 256;
  std::uint32_t seed = 127;
  bool dynamic_state = true;
  bool ambience = true;
};
Config adapt_config(const Hardware& hardware);
void validate_config(const Config& cfg);

struct Event {
  double seconds = 0;
  Surface surface = Surface::wood;
  double intensity = 0.8;   // [0, 1]
  double pan = 0;           // [-1, 1]
};
std::vector<Event> read_events_csv(const std::string& path);

struct RenderResult {
  std::vector<float> interleaved;
  State12 final_state{};
  float peak = 0;
  double rms = 0;
  std::uint64_t frames = 0;
};
RenderResult render(const Config& cfg, double seconds, std::vector<Event> events);
void write_wav16(const std::string& path, const Config& cfg, const RenderResult& result);
} // namespace cst_audio
