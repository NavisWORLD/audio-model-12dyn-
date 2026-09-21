#include "cst_audio/audio.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>

#if defined(_WIN32)
#  define NOMINMAX
#  include <windows.h>
#elif defined(__APPLE__)
#  include <sys/sysctl.h>
#elif defined(__linux__)
#  include <unistd.h>
#endif

namespace cst_audio {
namespace {
constexpr double pi = 3.14159265358979323846;
constexpr double theta = 0.17320508075688773;

double clamp(double x, double lo, double hi) { return std::max(lo, std::min(x, hi)); }
std::string trim(std::string s) {
  auto good = [](unsigned char c) { return !std::isspace(c); };
  auto a = std::find_if(s.begin(), s.end(), good);
  auto b = std::find_if(s.rbegin(), s.rend(), good).base();
  if (a >= b) return {};
  return std::string(a, b);
}
std::uint32_t mix(std::uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
  return x;
}
double noise(std::uint32_t seed, std::uint32_t frame) {
  return (static_cast<double>(mix(seed ^ mix(frame))) / 4294967295.0) * 2.0 - 1.0;
}
void write_u16(std::ofstream& out, std::uint16_t n) {
  for (int i = 0; i < 2; ++i) out.put(static_cast<char>((n >> (8 * i)) & 255));
}
void write_u32(std::ofstream& out, std::uint32_t n) {
  for (int i = 0; i < 4; ++i) out.put(static_cast<char>((n >> (8 * i)) & 255));
}
struct Voice {
  std::uint64_t start, duration;
  Surface surface;
  double intensity, pan;
  std::uint32_t seed;
};
} // namespace

State12 update_dyn12(const State12& state, const std::vector<double>& drive, std::uint64_t step) {
  State12 next{};
  for (std::size_t i = 0; i < next.size(); ++i) {
    double u = drive.empty() ? 0.0 : drive[i % drive.size()];
    if (!std::isfinite(u) || !std::isfinite(state[i])) throw std::invalid_argument("nonfinite dyn12 value");
    double forcing = 0.015 * std::sin(static_cast<double>(step + 1) * static_cast<double>(i + 1) * theta);
    next[i] = std::tanh(0.86 * state[i] + 0.14 * u + forcing);
  }
  return next;
}

Surface parse_surface(const std::string& name) {
  if (name == "wood") return Surface::wood;
  if (name == "gravel") return Surface::gravel;
  if (name == "metal") return Surface::metal;
  if (name == "concrete") return Surface::concrete;
  if (name == "fabric") return Surface::fabric;
  if (name == "water") return Surface::water;
  throw std::invalid_argument("unknown surface: " + name);
}
const char* surface_name(Surface s) {
  switch (s) {
    case Surface::wood: return "wood";
    case Surface::gravel: return "gravel";
    case Surface::metal: return "metal";
    case Surface::concrete: return "concrete";
    case Surface::fabric: return "fabric";
    case Surface::water: return "water";
  }
  return "unknown";
}

Hardware probe_hardware() {
  Hardware hw;
  hw.logical_cores = std::max(1u, std::thread::hardware_concurrency());
#if defined(_WIN32)
  hw.os = "windows";
  MEMORYSTATUSEX status{}; status.dwLength = sizeof(status);
  if (GlobalMemoryStatusEx(&status)) hw.physical_ram_bytes = status.ullTotalPhys;
#elif defined(__APPLE__)
  hw.os = "macos";
  std::uint64_t bytes = 0; size_t len = sizeof(bytes);
  if (sysctlbyname("hw.memsize", &bytes, &len, nullptr, 0) == 0) hw.physical_ram_bytes = bytes;
#elif defined(__linux__)
  hw.os = "linux";
  long pages = sysconf(_SC_PHYS_PAGES);
  long size = sysconf(_SC_PAGESIZE);
  if (pages > 0 && size > 0) hw.physical_ram_bytes = static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(size);
#else
  hw.os = "unknown";
#endif
  return hw;
}
Config adapt_config(const Hardware& hw) {
  Config cfg;
  // Conservative offline preset. No GPU assumptions, no claims of multithreaded DSP.
  const bool low_memory = hw.physical_ram_bytes && hw.physical_ram_bytes <= (2ull << 30);
  if (hw.logical_cores <= 2 || low_memory) { cfg.sample_rate = 32000; cfg.block_frames = 512; }
  else if (hw.logical_cores <= 4) cfg.block_frames = 512;
  return cfg;
}
void validate_config(const Config& cfg) {
  if (cfg.sample_rate < 8000 || cfg.sample_rate > 192000) throw std::invalid_argument("sample rate must be 8000..192000");
  if (cfg.channels != 1 && cfg.channels != 2) throw std::invalid_argument("channels must be 1 or 2");
  if (cfg.block_frames < 32 || cfg.block_frames > 4096) throw std::invalid_argument("block frames must be 32..4096");
}

std::vector<Event> read_events_csv(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot open events CSV: " + path);
  std::vector<Event> events;
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(in, line)) {
    ++line_number;
    if (trim(line).empty() || trim(line)[0] == '#') continue;
    if (line_number == 1 && line.find("seconds") != std::string::npos) continue;
    std::istringstream parts(line); std::string fields[4];
    bool complete = true;
    for (auto& field : fields) if (!std::getline(parts, field, ',')) complete = false;
    if (!complete || parts.rdbuf()->in_avail() != 0) throw std::invalid_argument("CSV: expected 4 fields at line " + std::to_string(line_number));
    Event e;
    try {
      std::size_t consumed = 0;
      auto strict_double = [&](const std::string& raw) {
        const std::string value = trim(raw);
        double out = std::stod(value, &consumed);
        if (consumed != value.size() || !std::isfinite(out)) throw std::invalid_argument("not finite numeric");
        return out;
      };
      e.seconds = strict_double(fields[0]);
      e.surface = parse_surface(trim(fields[1]));
      e.intensity = strict_double(fields[2]);
      e.pan = strict_double(fields[3]);
    } catch (const std::exception&) { throw std::invalid_argument("CSV: invalid event at line " + std::to_string(line_number)); }
    if (e.seconds < 0 || e.intensity < 0 || e.intensity > 1 || e.pan < -1 || e.pan > 1)
      throw std::invalid_argument("CSV: event out of range at line " + std::to_string(line_number));
    if (events.size() >= 100000) throw std::invalid_argument("too many events");
    events.push_back(e);
  }
  return events;
}

RenderResult render(const Config& cfg, double seconds, std::vector<Event> events) {
  validate_config(cfg);
  if (!std::isfinite(seconds) || seconds <= 0 || seconds > 600) throw std::invalid_argument("duration must be >0 and <=600 seconds");
  const auto frames = static_cast<std::uint64_t>(std::ceil(seconds * cfg.sample_rate));
  if (frames > std::numeric_limits<std::uint32_t>::max() / (2u * cfg.channels)) throw std::invalid_argument("WAV exceeds 4GB limit");
  std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) { return a.seconds < b.seconds; });
  std::vector<Voice> voices;
  voices.reserve(events.size());
  for (std::size_t i = 0; i < events.size(); ++i) {
    const auto& e = events[i];
    if (!std::isfinite(e.seconds) || e.seconds < 0 || !std::isfinite(e.intensity) || e.intensity < 0 || e.intensity > 1 || !std::isfinite(e.pan) || e.pan < -1 || e.pan > 1) throw std::invalid_argument("invalid event");
    if (e.seconds >= seconds) continue;
    const double decay = e.surface == Surface::metal ? 0.42 : (e.surface == Surface::water ? 0.34 : 0.27);
    voices.push_back({static_cast<std::uint64_t>(std::llround(e.seconds * cfg.sample_rate)),
                     static_cast<std::uint64_t>(std::ceil(decay * cfg.sample_rate)), e.surface, e.intensity, e.pan,
                     mix(cfg.seed + static_cast<std::uint32_t>(i * 7919u) + 0x9e3779b9u)});
  }
  RenderResult result;
  result.frames = frames;
  result.interleaved.resize(static_cast<std::size_t>(frames) * cfg.channels, 0);
  std::size_t next_voice = 0;
  std::vector<std::size_t> active;
  State12 state{};
  std::uint64_t block_index = 0;
  double sum_squares = 0;
  for (std::uint64_t base = 0; base < frames; base += cfg.block_frames, ++block_index) {
    const auto end = std::min(frames, base + cfg.block_frames);
    // Event features drive dyn12 once per block, preventing expensive per-sample state updates.
    double activity = 0, hardness = 0, pan = 0, transient = 0;
    for (const auto& v : voices) {
      if (v.start < end && v.start + v.duration > base) {
        activity += v.intensity;
        pan += v.pan * v.intensity;
        if (v.start >= base && v.start < end) transient += v.intensity;
        hardness += (v.surface == Surface::metal || v.surface == Surface::concrete ? 1.0 : 0.3) * v.intensity;
      }
    }
    const std::vector<double> drive{clamp(activity, 0, 1), clamp(hardness, 0, 1), clamp(pan, -1, 1), clamp(transient, 0, 1)};
    if (cfg.dynamic_state) state = update_dyn12(state, drive, block_index);
    const double timbre = cfg.dynamic_state ? state[1] : 0;
    const double texture = cfg.dynamic_state ? state[4] : 0;
    for (auto frame = base; frame < end; ++frame) {
      while (next_voice < voices.size() && voices[next_voice].start <= frame) active.push_back(next_voice++);
      active.erase(std::remove_if(active.begin(), active.end(), [&](std::size_t idx) {
        return frame >= voices[idx].start + voices[idx].duration;
      }), active.end());
      double left = 0, right = 0;
      for (auto idx : active) {
        const auto& v = voices[idx];
        const auto t = static_cast<double>(frame - v.start) / cfg.sample_rate;
        const double env = std::exp(-t * (v.surface == Surface::metal ? 9.0 : 15.0)) * std::min(1.0, t * 450.0);
        const auto n = static_cast<std::uint32_t>(frame - v.start);
        const double raw = noise(v.seed, n);
        // Different deterministic acoustic materials; no recordings or third-party assets.
        double body = 0, surface = 0;
        switch (v.surface) {
          case Surface::wood:
            body = std::sin(2 * pi * (95.0 + 22.0 * timbre) * t) * std::exp(-t * 17.0);
            surface = raw * 0.45; break;
          case Surface::gravel:
            body = std::sin(2 * pi * 74 * t) * std::exp(-t * 18.0) * 0.4;
            surface = raw * (0.8 + 0.2 * std::sin(2 * pi * 61 * t)); break;
          case Surface::metal:
            body = (std::sin(2 * pi * (420 + 20 * timbre) * t) + 0.45 * std::sin(2 * pi * 873 * t)) * std::exp(-t * 10.0) * 0.55;
            surface = raw * 0.38; break;
          case Surface::concrete:
            body = std::sin(2 * pi * 67 * t) * std::exp(-t * 25.0) * 0.65;
            surface = raw * 0.46; break;
          case Surface::fabric:
            body = std::sin(2 * pi * 51 * t) * std::exp(-t * 21.0) * 0.24;
            surface = (raw + noise(v.seed, n / 5)) * 0.14; break;
          case Surface::water:
            body = std::sin(2 * pi * 131 * t) * std::exp(-t * 19.0) * 0.2;
            surface = raw * (0.43 + 0.15 * std::sin(2 * pi * 23 * t)); break;
        }
        const double sample = v.intensity * env * (0.55 * body + (0.38 + texture * 0.08) * surface);
        const double a = (v.pan + 1.0) * pi / 4.0;
        left += sample * std::cos(a);
        right += sample * std::sin(a);
      }
      if (cfg.ambience) {
        const double bed = 0.004 * (noise(cfg.seed ^ 0xace5u, static_cast<std::uint32_t>(frame / 16)) + noise(cfg.seed ^ 0x9ff4u, static_cast<std::uint32_t>(frame / 49)));
        left += bed; right += bed;
      }
      for (unsigned ch = 0; ch < cfg.channels; ++ch) {
        const double value = cfg.channels == 1 ? (left + right) * 0.7071067811865475 : (ch == 0 ? left : right);
        // Explicit clip guard, avoid wrap in PCM conversion. Clipping is still reported by peak.
        result.peak = std::max(result.peak, static_cast<float>(std::abs(value)));
        const float final_value = static_cast<float>(clamp(value, -1, 1));
        result.interleaved[static_cast<std::size_t>(frame) * cfg.channels + ch] = final_value;
        sum_squares += final_value * final_value;
      }
    }
  }
  result.final_state = state;
  result.rms = std::sqrt(sum_squares / static_cast<double>(frames * cfg.channels));
  return result;
}

void write_wav16(const std::string& path, const Config& cfg, const RenderResult& result) {
  validate_config(cfg);
  if (result.interleaved.size() != result.frames * cfg.channels) throw std::invalid_argument("WAV shape mismatch");
  if (result.frames > (std::numeric_limits<std::uint32_t>::max() - 36) / (cfg.channels * 2)) throw std::invalid_argument("WAV too large");
  std::ofstream out(path, std::ios::binary);
  if (!out) throw std::runtime_error("cannot write WAV: " + path);
  const auto data_bytes = static_cast<std::uint32_t>(result.interleaved.size() * 2);
  out.write("RIFF", 4); write_u32(out, data_bytes + 36); out.write("WAVEfmt ", 8);
  write_u32(out, 16); write_u16(out, 1); write_u16(out, static_cast<std::uint16_t>(cfg.channels));
  write_u32(out, cfg.sample_rate); write_u32(out, cfg.sample_rate * cfg.channels * 2);
  write_u16(out, static_cast<std::uint16_t>(cfg.channels * 2)); write_u16(out, 16);
  out.write("data", 4); write_u32(out, data_bytes);
  for (float sample : result.interleaved) {
    if (!std::isfinite(sample)) throw std::invalid_argument("nonfinite audio sample");
    const double clamped = clamp(sample, -1.0, 1.0);
    const std::int16_t pcm = static_cast<std::int16_t>(std::lround(clamped * (clamped < 0 ? 32768.0 : 32767.0)));
    write_u16(out, static_cast<std::uint16_t>(pcm));
  }
  if (!out) throw std::runtime_error("failed writing WAV: " + path);
}
} // namespace cst_audio
