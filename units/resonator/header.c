/*
    BSD 3-Clause License

    Copyright (c) 2023, KORG INC.
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this
      list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.

    * Neither the name of the copyright holder nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
    FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
    OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

//*/

// Resonator unit header: Mutable Instruments Rings for the NTS-3 kaoss pad kit.
// Derived from the logue SDK dummy-genericfx template (BSD 3-Clause, KORG).

#include "unit_genericfx.h"

// Metered diagnostic builds (units/common/meter.h) take their own unit ID, so
// the device lists them next to the regular unit instead of merging the two.
#ifdef NTS3_METER
#define UNIT_ID(id) ((id) | 0x40U)
#else
#define UNIT_ID(id) (id)
#endif

const __unit_header genericfx_unit_header_t unit_header = {
  .common = {
    .header_size = sizeof(genericfx_unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_genericfx,
    .api = UNIT_API_VERSION,
    .dev_id = 0x30784D43U,  // '0xMC'
    .unit_id = UNIT_ID(0x1U),
    .version = 0x00010000U,
    .name = "Resonator",
    .num_params = 8,

    .params = {
      // Format: min, max, center, default, type, frac. bits, frac. mode, <reserved>, name
      {24, 96, 60, 48, k_unit_param_type_midi_note, 0, 0, 0, {"Note"}},
      {0, 1000, 0, 250, k_unit_param_type_percent, 1, 1, 0, {"Structure"}},
      {-1000, 1000, 0, 500, k_unit_param_type_drywet, 1, 1, 0, {"Mix"}},
      {0, 1000, 0, 500, k_unit_param_type_percent, 1, 1, 0, {"Brightness"}},
      {0, 1000, 0, 500, k_unit_param_type_percent, 1, 1, 0, {"Damping"}},
      {0, 1000, 0, 500, k_unit_param_type_percent, 1, 1, 0, {"Position"}},
      {0, 5, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Model"}},
      {0, 2, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Polyphony"}}},
  },
  .default_mappings = {
    // Format: assign, curve, curve polarity, min, max, default value
    // X: four octaves of semitones from C2.
    {k_genericfx_param_assign_x, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 36, 84, 48},
    {k_genericfx_param_assign_y, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1000, 250},
    {k_genericfx_param_assign_depth, k_genericfx_curve_linear, k_genericfx_curve_unipolar, -1000, 1000, 500},
    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1000, 500},
    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1000, 500},
    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1000, 500},
    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 5, 0},
    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 2, 0}
  }
};
