#include "cst_audio/audio.hpp"

#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace cst_audio;
namespace {
void usage() {
  std::cout << "CST Audio 12dyn - offline, dependency-free C++17 procedural Foley\n"
            << "  cst-foley --probe\n"
            << "  cst-foley --render --events path.csv --out footsteps.wav [--seconds 5]\n"
            << "  cst-foley --render [--sample-rate 48000] [--channels 2] [--block 256]\n"
            << "            [--seed 127] [--static] [--no-ambience]\n";
}
unsigned parse_unsigned(const std::string& s) {
  std::size_t pos = 0;
  const auto n = std::stoull(s, &pos);
  if (pos != s.size() || n > 0xffffffffull) throw std::invalid_argument("invalid unsigned argument: " + s);
  return static_cast<unsigned>(n);
}
}
int main(int argc, char** argv) {
  try {
    if (argc == 1) { usage(); return 0; }
    Hardware hw = probe_hardware();
    Config cfg = adapt_config(hw);
    bool probe = false, generate = false;
    std::string events_path, out = "footsteps.wav";
    double seconds = 4.0;
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      auto next = [&]() -> std::string {
        if (++i >= argc) throw std::invalid_argument("missing value after " + arg);
        return argv[i];
      };
      if (arg == "--help" || arg == "-h") { usage(); return 0; }
      else if (arg == "--probe") probe = true;
      else if (arg == "--render") generate = true;
      else if (arg == "--events") events_path = next();
      else if (arg == "--out") out = next();
      else if (arg == "--seconds") { std::string v = next(); std::size_t pos = 0; seconds = std::stod(v, &pos); if (pos != v.size()) throw std::invalid_argument("invalid duration"); }
      else if (arg == "--sample-rate") cfg.sample_rate = parse_unsigned(next());
      else if (arg == "--channels") cfg.channels = parse_unsigned(next());
      else if (arg == "--block") cfg.block_frames = parse_unsigned(next());
      else if (arg == "--seed") cfg.seed = parse_unsigned(next());
      else if (arg == "--static") cfg.dynamic_state = false;
      else if (arg == "--no-ambience") cfg.ambience = false;
      else throw std::invalid_argument("unknown argument: " + arg);
    }
    validate_config(cfg);
    if (probe) {
      std::cout << "os=" << hw.os << " logical_cores=" << hw.logical_cores
                << " ram_bytes=" << hw.physical_ram_bytes << " (0=unknown)"
                << " default_sample_rate=" << adapt_config(hw).sample_rate
                << " default_block=" << adapt_config(hw).block_frames
                << " gpu_backend=none process_threads=1\n";
    }
    if (!generate) return 0;
    std::vector<Event> events;
    if (events_path.empty()) {
      events = {{0.25, Surface::wood, 0.75, -0.45}, {0.82, Surface::wood, 0.9, 0.45},
                {1.40, Surface::gravel, 0.7, -0.3}, {2.05, Surface::metal, 0.85, 0.3},
                {2.75, Surface::water, 0.65, -0.15}, {3.28, Surface::fabric, 0.8, 0.15}};
    } else events = read_events_csv(events_path);
    const auto output = render(cfg, seconds, events);
    write_wav16(out, cfg, output);
    std::cout << std::fixed << std::setprecision(6)
              << "rendered=" << out << " frames=" << output.frames << " sample_rate=" << cfg.sample_rate
              << " channels=" << cfg.channels << " events=" << events.size()
              << " dyn12=" << (cfg.dynamic_state ? "on" : "off")
              << " peak_preclip=" << output.peak << " rms=" << output.rms << "\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "error: " << ex.what() << "\n";
    return 1;
  }
}
