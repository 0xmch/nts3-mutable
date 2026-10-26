// Offline harness: runs an NTS-3 unit on the host through the same unit_* entry
// points the firmware calls, and writes the result as a WAV file.
//
//   harness -i in.wav -o out.wav [-s script.txt] [-l seconds] [-f frames]
//           [-r release seconds]
//
// Without -i the input is silence. The script holds one event per line:
//   <seconds> param <id> <value>
//   <seconds> touch <began|moved|ended> [x y]
// It prints peak/RMS per second of output, NaN/Inf counts, and the render time
// relative to real time.
//
// The firmware's raw-input hook (get_raw_input) always returns the block's
// input. -r routes the unit as the NTS-3 does (Release Mode "thru", measured
// on the device): it is called all the time and its output is always added,
// but its input is crossfaded with the dry path, fully to the unit while the
// pad is touched and back to the dry path over the release time after.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "unit_genericfx.h"

extern "C" {
int8_t unit_init(const unit_runtime_desc_t* desc);
void unit_render(const float* in, float* out, uint32_t frames);
void unit_set_param_value(uint8_t id, int32_t value);
const char* unit_get_param_str_value(uint8_t id, int32_t value);
void unit_touch_event(uint8_t id, uint8_t phase, uint32_t x, uint32_t y);
void unit_teardown();
}

namespace {

const uint32_t kSampleRate = 48000;
size_t sdram_used = 0;
const size_t kSdramPerRuntime = 3 * 1024 * 1024;

uint8_t* SdramAlloc(size_t size) {
  sdram_used += size;
  if (sdram_used > kSdramPerRuntime) {
    std::fprintf(stderr, "sdram_alloc: %zu bytes exceeds the 3 MB runtime budget\n",
                 sdram_used);
    return nullptr;
  }
  return static_cast<uint8_t*>(std::calloc(1, size));
}
void SdramFree(const uint8_t* mem) { std::free(const_cast<uint8_t*>(mem)); }

// get_raw_input: the current block's input, before the pad's routing.
const float* raw_input = nullptr;
const float* RawInput() { return raw_input; }
size_t SdramAvail() { return kSdramPerRuntime - sdram_used; }

// Minimal WAV I/O: 16-bit PCM or 32-bit float, stereo or mono, 48 kHz.
bool ReadWav(const char* path, std::vector<float>* stereo) {
  FILE* f = std::fopen(path, "rb");
  if (!f) return false;
  char riff[12];
  if (std::fread(riff, 1, 12, f) != 12 || std::memcmp(riff, "RIFF", 4) ||
      std::memcmp(riff + 8, "WAVE", 4)) {
    std::fclose(f);
    return false;
  }
  uint16_t format = 0, channels = 0, bits = 0;
  uint32_t rate = 0;
  char id[4];
  uint32_t size;
  while (std::fread(id, 1, 4, f) == 4 && std::fread(&size, 4, 1, f) == 1) {
    if (!std::memcmp(id, "fmt ", 4)) {
      std::vector<uint8_t> fmt(std::max<uint32_t>(size, 16));
      if (size < 16 || std::fread(fmt.data(), 1, size, f) != size) break;
      if (size & 1) std::fseek(f, 1, SEEK_CUR);
      std::memcpy(&format, &fmt[0], 2);
      std::memcpy(&channels, &fmt[2], 2);
      std::memcpy(&rate, &fmt[4], 4);
      std::memcpy(&bits, &fmt[14], 2);
      if (format == 0xFFFE && size >= 26) std::memcpy(&format, &fmt[24], 2);
    } else if (!std::memcmp(id, "data", 4)) {
      if (rate != kSampleRate || (channels != 1 && channels != 2) ||
          !((format == 1 && bits == 16) || (format == 3 && bits == 32))) {
        std::fprintf(stderr, "%s: need 48 kHz mono/stereo PCM16 or float32\n", path);
        std::fclose(f);
        return false;
      }
      size_t frames = size / (bits / 8) / channels;
      stereo->resize(frames * 2);
      for (size_t i = 0; i < frames; ++i) {
        for (int c = 0; c < 2; ++c) {
          float v = 0.0f;
          if (c < channels) {
            bool read = false;
            if (format == 1) {
              int16_t s;
              read = std::fread(&s, 2, 1, f) == 1;
              v = s / 32768.0f;
            } else {
              read = std::fread(&v, 4, 1, f) == 1;
            }
            if (!read) {  // truncated file: keep what was read
              stereo->resize(i * 2);
              std::fclose(f);
              return i > 0;
            }
          } else {
            v = (*stereo)[i * 2];
          }
          (*stereo)[i * 2 + c] = v;
        }
      }
      std::fclose(f);
      return true;
    } else {
      std::fseek(f, size + (size & 1), SEEK_CUR);
    }
  }
  std::fclose(f);
  return false;
}

bool WriteWav(const char* path, const std::vector<float>& stereo) {
  FILE* f = std::fopen(path, "wb");
  if (!f) return false;
  uint32_t data_size = stereo.size() * 4;
  uint32_t riff_size = 36 + data_size;
  uint16_t format = 3, channels = 2, block = 8, bits = 32;
  uint32_t rate = kSampleRate, byte_rate = kSampleRate * 8, fmt_size = 16;
  std::fwrite("RIFF", 1, 4, f);
  std::fwrite(&riff_size, 4, 1, f);
  std::fwrite("WAVEfmt ", 1, 8, f);
  std::fwrite(&fmt_size, 4, 1, f);
  std::fwrite(&format, 2, 1, f);
  std::fwrite(&channels, 2, 1, f);
  std::fwrite(&rate, 4, 1, f);
  std::fwrite(&byte_rate, 4, 1, f);
  std::fwrite(&block, 2, 1, f);
  std::fwrite(&bits, 2, 1, f);
  std::fwrite("data", 1, 4, f);
  std::fwrite(&data_size, 4, 1, f);
  std::fwrite(stereo.data(), 4, stereo.size(), f);
  std::fclose(f);
  return true;
}

struct Event {
  double time;
  bool touch;
  uint8_t id_or_phase;
  int32_t value;
  uint32_t x, y;
};

std::vector<Event> ReadScript(const char* path) {
  std::vector<Event> events;
  FILE* f = std::fopen(path, "r");
  if (!f) {
    std::fprintf(stderr, "cannot open script %s\n", path);
    std::exit(1);
  }
  char line[256];
  while (std::fgets(line, sizeof(line), f)) {
    char kind[16] = {0}, arg[16] = {0};
    double t;
    int a = 0, b = 0, c = 0;
    if (line[0] == '#' || std::sscanf(line, "%lf %15s", &t, kind) != 2) continue;
    Event e = {t, false, 0, 0, 512, 512};
    int n = 0;
    if (!std::strcmp(kind, "param") &&
        std::sscanf(line, "%lf %15s %d %d", &t, kind, &a, &b) == 4 && a >= 0 &&
        a < static_cast<int>(unit_header.common.num_params)) {
      e.id_or_phase = a;
      e.value = b;
    } else if (!std::strcmp(kind, "touch") &&
               (n = std::sscanf(line, "%lf %15s %15s %d %d", &t, kind, arg, &b, &c)) >= 3 &&
               (!std::strcmp(arg, "began") || !std::strcmp(arg, "moved") ||
                !std::strcmp(arg, "ended"))) {
      e.touch = true;
      e.id_or_phase = !std::strcmp(arg, "began") ? k_unit_touch_phase_began
                      : !std::strcmp(arg, "moved") ? k_unit_touch_phase_moved
                                                    : k_unit_touch_phase_ended;
      if (n == 5) {
        e.x = b;
        e.y = c;
      }
    } else {
      std::fprintf(stderr, "bad script line: %s", line);
      std::exit(1);
    }
    events.push_back(e);
  }
  std::fclose(f);
  return events;
}

}  // namespace

int main(int argc, char** argv) {
  const char* in_path = nullptr;
  const char* out_path = "out.wav";
  const char* script_path = nullptr;
  double length = -1.0;
  uint32_t frames = 64;
  double release = -1.0;
  for (int i = 1; i + 1 < argc; i += 2) {
    std::string flag = argv[i];
    if (flag == "-i") in_path = argv[i + 1];
    else if (flag == "-o") out_path = argv[i + 1];
    else if (flag == "-s") script_path = argv[i + 1];
    else if (flag == "-l") length = std::atof(argv[i + 1]);
    else if (flag == "-f") frames = std::atoi(argv[i + 1]);
    else if (flag == "-r") release = std::atof(argv[i + 1]);
  }
  if (frames < 1 || frames > 4096) {
    std::fprintf(stderr, "-f: frames per call must be 1 to 4096\n");
    return 1;
  }

  std::vector<float> input;
  if (in_path && !ReadWav(in_path, &input)) {
    std::fprintf(stderr, "cannot read %s\n", in_path);
    return 1;
  }
  size_t total = length > 0 ? static_cast<size_t>(length * kSampleRate)
                            : input.size() / 2;
  if (!total) total = 4 * kSampleRate;
  input.resize(total * 2, 0.0f);
  std::vector<Event> events;
  if (script_path) events = ReadScript(script_path);

  unit_runtime_genericfx_context_t context = {};
  context.touch_area_width = 1024;
  context.touch_area_height = 1024;
  context.get_raw_input = RawInput;
  unit_runtime_desc_t desc = {};
  desc.target = unit_header.common.target;
  desc.api = UNIT_API_VERSION;
  desc.samplerate = kSampleRate;
  desc.frames_per_buffer = frames;
  desc.input_channels = 2;
  desc.output_channels = 2;
  desc.hooks.runtime_context = &context;
  desc.hooks.sdram_alloc = SdramAlloc;
  desc.hooks.sdram_free = SdramFree;
  desc.hooks.sdram_avail = SdramAvail;
  int8_t err = unit_init(&desc);
  if (err != k_unit_err_none) {
    std::fprintf(stderr, "unit_init failed: %d\n", err);
    return 1;
  }
  std::printf("unit %s, SDRAM %.1f KB\n", unit_header.common.name, sdram_used / 1024.0);

  std::vector<float> output(total * 2, 0.0f);
  size_t next_event = 0;
  const bool nts3_mode = release >= 0.0;
  const size_t release_frames = nts3_mode ? static_cast<size_t>(release * kSampleRate) : 0;
  bool touched = false;
  size_t tail_left = 0;
  std::vector<float> unit_in(frames * 2, 0.0f);
  double render_seconds = 0.0;
  double slowest_share = 0.0, slowest_at = 0.0;
  for (size_t pos = 0; pos < total; pos += frames) {
    double now = static_cast<double>(pos) / kSampleRate;
    while (next_event < events.size() && events[next_event].time <= now) {
      const Event& e = events[next_event++];
      if (e.touch) {
        unit_touch_event(0, e.id_or_phase, e.x, e.y);
        if (e.id_or_phase == k_unit_touch_phase_began) touched = true;
        if (e.id_or_phase == k_unit_touch_phase_ended) {
          touched = false;
          tail_left = release_frames;
        }
      } else {
        unit_set_param_value(e.id_or_phase, e.value);
        const char* s = unit_get_param_str_value(e.id_or_phase, e.value);
        std::printf("%7.3f  %s = %d%s%s\n", now,
                    unit_header.common.params[e.id_or_phase].name, e.value,
                    s ? " " : "", s ? s : "");
      }
    }
    uint32_t n = static_cast<uint32_t>(std::min<size_t>(frames, total - pos));
    auto t0 = std::chrono::steady_clock::now();
    raw_input = &input[pos * 2];
    if (!nts3_mode || touched) {
      unit_render(&input[pos * 2], &output[pos * 2], n);
    } else {
      // Routing gain g: from 1 at the release down to 0 over the release time.
      for (uint32_t i = 0; i < 2 * n; ++i) {
        float g = tail_left > i / 2 ? static_cast<float>(tail_left - i / 2) / release_frames : 0.0f;
        unit_in[i] = g * input[pos * 2 + i];
      }
      unit_render(unit_in.data(), &output[pos * 2], n);
      for (uint32_t i = 0; i < 2 * n; ++i) {
        float g = tail_left > i / 2 ? static_cast<float>(tail_left - i / 2) / release_frames : 0.0f;
        output[pos * 2 + i] += (1.0f - g) * input[pos * 2 + i];
      }
      tail_left = tail_left > n ? tail_left - n : 0;
    }
    double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    render_seconds += seconds;
    // The slowest call against its own duration: what overloads the device
    // is a call that overruns, not the average.
    double share = seconds / (static_cast<double>(n) / kSampleRate);
    if (share > slowest_share) {
      slowest_share = share;
      slowest_at = static_cast<double>(pos) / kSampleRate;
    }
  }
  unit_teardown();

  size_t bad = 0;
  for (size_t s = 0; s < total; s += kSampleRate) {
    float peak = 0.0f;
    double sum = 0.0;
    size_t end = std::min(total, s + kSampleRate);
    for (size_t i = s * 2; i < end * 2; ++i) {
      float v = output[i];
      if (!std::isfinite(v)) {
        ++bad;
        output[i] = 0.0f;
        continue;
      }
      peak = std::max(peak, std::fabs(v));
      sum += v * v;
    }
    double rms = std::sqrt(sum / ((end - s) * 2));
    std::printf("%5.1fs  peak %6.1f dBFS  rms %6.1f dBFS\n",
                static_cast<double>(s) / kSampleRate,
                20.0 * std::log10(std::max(peak, 1e-9f)),
                20.0 * std::log10(std::max(rms, 1e-9)));
  }
  std::printf("non-finite samples: %zu\n", bad);
  std::printf("render: %.2f%% of real time on this host, slowest call %.1f%% at %.2fs\n",
              100.0 * render_seconds / (static_cast<double>(total) / kSampleRate),
              100.0 * slowest_share, slowest_at);
  if (!WriteWav(out_path, output)) {
    std::fprintf(stderr, "cannot write %s\n", out_path);
    return 1;
  }
  return bad ? 2 : 0;
}
