// Web build of a unit: its Effect class behind a small C API that an
// AudioWorklet calls directly. Built as standalone WebAssembly (no emscripten
// runtime, no SharedArrayBuffer), so the page needs no special headers.

#include <stdlib.h>

#include "effect.h"

namespace {

const int kMaxFrames = 128;
Effect effect;
float in_buffer[2 * kMaxFrames];
float out_buffer[2 * kMaxFrames];
char json[8192];
size_t json_size;

void Put(const char* s) {
  while (*s && json_size + 1 < sizeof(json)) json[json_size++] = *s++;
  json[json_size] = '\0';
}

void PutInt(int32_t v) {
  char digits[12];
  int n = 0;
  bool negative = v < 0;
  uint32_t u = negative ? 0u - static_cast<uint32_t>(v) : static_cast<uint32_t>(v);
  do {
    digits[n++] = static_cast<char>('0' + u % 10);
    u /= 10;
  } while (u);
  if (negative) Put("-");
  char out[2] = {0, 0};
  while (n) {
    out[0] = digits[--n];
    Put(out);
  }
}

void PutString(const char* s) {
  Put("\"");
  char out[2] = {0, 0};
  for (; *s; ++s) {
    if (*s == '"' || *s == '\\') Put("\\");
    out[0] = *s;
    Put(out);
  }
  Put("\"");
}

}  // namespace

extern "C" {

__attribute__((export_name("nts3_in"))) float* nts3_in() { return in_buffer; }
__attribute__((export_name("nts3_out"))) float* nts3_out() { return out_buffer; }
__attribute__((export_name("nts3_max_frames"))) int nts3_max_frames() {
  return kMaxFrames;
}

// Allocates the unit's SDRAM block (zeroed, as on the device) and runs init.
__attribute__((export_name("nts3_init"))) int nts3_init() {
  uint32_t floats = effect.getBufferSize();
  float* ram = floats ? static_cast<float*>(calloc(floats, sizeof(float)))
                      : nullptr;
  if (floats && !ram) return 0;
  effect.init(ram);
  return 1;
}

__attribute__((export_name("nts3_process"))) void nts3_process(int frames) {
  effect.process(in_buffer, out_buffer, frames);
}

__attribute__((export_name("nts3_set_param"))) void nts3_set_param(int id,
                                                                   int value) {
  if (id < 0 || id >= static_cast<int>(unit_header.common.num_params)) return;
  const unit_param_t& p = unit_header.common.params[id];
  if (value < p.min) value = p.min;
  if (value > p.max) value = p.max;
  effect.setParameter(id, value);
}

__attribute__((export_name("nts3_touch"))) void nts3_touch(int phase, int x,
                                                           int y) {
  effect.touchEvent(0, phase, x, y);
}

// Display text of a strings-type parameter value, for those with too many
// values to list in the description (marked "dynamic" there).
__attribute__((export_name("nts3_param_str"))) const char* nts3_param_str(
    int id, int value) {
  if (id < 0 || id >= static_cast<int>(unit_header.common.num_params)) return "";
  const char* s = effect.getParameterStrValue(id, value);
  return s ? s : "";
}

// The unit header as JSON: name, parameters (with the strings of small
// strings-type parameters) and default pad mappings.
__attribute__((export_name("nts3_describe"))) const char* nts3_describe() {
  json_size = 0;
  const unit_header_t& h = unit_header.common;
  Put("{\"name\":");
  PutString(h.name);
  Put(",\"unit_id\":");
  PutInt(h.unit_id);
  Put(",\"version\":");
  PutInt(h.version);
  Put(",\"params\":[");
  for (uint32_t i = 0; i < h.num_params; ++i) {
    const unit_param_t& p = h.params[i];
    if (i) Put(",");
    Put("{\"name\":");
    PutString(p.name);
    Put(",\"min\":");
    PutInt(p.min);
    Put(",\"max\":");
    PutInt(p.max);
    Put(",\"init\":");
    PutInt(p.init);
    Put(",\"type\":");
    PutInt(p.type);
    Put(",\"frac\":");
    PutInt(p.frac);
    Put(",\"frac_mode\":");
    PutInt(p.frac_mode);
    if (p.type == k_unit_param_type_strings && p.max - p.min > 64) {
      Put(",\"dynamic\":1");
    } else if (p.type == k_unit_param_type_strings) {
      Put(",\"strings\":[");
      for (int32_t v = p.min; v <= p.max; ++v) {
        const char* s = effect.getParameterStrValue(i, v);
        if (v != p.min) Put(",");
        PutString(s ? s : "");
      }
      Put("]");
    }
    const genericfx_param_mapping_t& m = unit_header.default_mappings[i];
    Put(",\"map\":{\"assign\":");
    PutInt(m.assign);
    Put(",\"curve\":");
    PutInt(m.curve);
    Put(",\"bipolar\":");
    PutInt(m.curve_polarity);
    Put(",\"min\":");
    PutInt(m.min);
    Put(",\"max\":");
    PutInt(m.max);
    Put(",\"value\":");
    PutInt(m.value);
    Put("}}");
  }
  Put("]}");
  return json;
}

}  // extern "C"
