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

// Meta Mod / Freq Shifter unit header: Mutable Instruments Warps for the NTS-3
// kaoss pad kit. Derived from the logue SDK dummy-genericfx template (BSD
// 3-Clause, KORG). WARPS_SHIFTER picks the unit: 0 is Warps (cross-modulation
// and vocoder), 1 its easter egg, the frequency shifter.

#include "unit_genericfx.h"

// Metered diagnostic builds (units/common/meter.h) take their own unit ID, so
// the device lists them next to the regular unit instead of merging the two.
#ifdef NTS3_METER
#define UNIT_ID(id) ((id) | 0x40U)
#else
#define UNIT_ID(id) (id)
#endif

#ifndef WARPS_SHIFTER
#define WARPS_SHIFTER 0
#endif

#define NO_PARAM {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}}
#define NO_MAPPING {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 0, 0}

const __unit_header genericfx_unit_header_t unit_header = {
  .common = {
    .header_size = sizeof(genericfx_unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_genericfx,
    .api = UNIT_API_VERSION,
    .dev_id = 0x30784D43U,  // '0xMC'
#if WARPS_SHIFTER
    .unit_id = UNIT_ID(0x6U),
    .version = 0x00010000U,
    .name = "Freq Shifter",
    .num_params = 5,
    .params = {
      // Format: min, max, center, default, type, frac. bits, frac. mode, <reserved>, name
      {0, 1000, 500, 500, k_unit_param_type_strings, 0, 0, 0, {"Shift"}},
      {0, 1000, 0, 300, k_unit_param_type_percent, 1, 1, 0, {"Feedback"}},
      {-1000, 1000, 0, 1000, k_unit_param_type_drywet, 1, 1, 0, {"Mix"}},
      {0, 20, 10, 20, k_unit_param_type_strings, 0, 0, 0, {"Sideband"}},
      {0, 3, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Carrier"}},
      NO_PARAM, NO_PARAM, NO_PARAM},
  },
  .default_mappings = {
    // Format: assign, curve, curve polarity, min, max, default value
    {k_genericfx_param_assign_x, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1000, 500},
    {k_genericfx_param_assign_y, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1000, 300},
    {k_genericfx_param_assign_depth, k_genericfx_curve_linear, k_genericfx_curve_unipolar, -1000, 1000, 1000},
    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 20, 20},
    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 3, 0},
    NO_MAPPING, NO_MAPPING, NO_MAPPING
  }
#else
    .unit_id = UNIT_ID(0x5U),
    .version = 0x00010000U,
    .name = "Meta Mod",
    .num_params = 7,
    .params = {
      // Format: min, max, center, default, type, frac. bits, frac. mode, <reserved>, name
      {0, 1000, 0, 300, k_unit_param_type_percent, 1, 1, 0, {"Algorithm"}},
      {0, 1000, 0, 500, k_unit_param_type_percent, 1, 1, 0, {"Timbre"}},
      {-1000, 1000, 0, 1000, k_unit_param_type_drywet, 1, 1, 0, {"Mix"}},
      {36, 96, 60, 60, k_unit_param_type_midi_note, 0, 0, 0, {"Note"}},
      {0, 3, 0, 1, k_unit_param_type_strings, 0, 0, 0, {"Carrier"}},
      {0, 1000, 0, 700, k_unit_param_type_percent, 1, 1, 0, {"Mod Drive"}},
      {0, 1000, 0, 700, k_unit_param_type_percent, 1, 1, 0, {"Carrier Drive"}},
      NO_PARAM},
  },
  .default_mappings = {
    // Format: assign, curve, curve polarity, min, max, default value
    {k_genericfx_param_assign_x, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1000, 300},
    {k_genericfx_param_assign_y, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1000, 500},
    {k_genericfx_param_assign_depth, k_genericfx_curve_linear, k_genericfx_curve_unipolar, -1000, 1000, 1000},
    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 36, 96, 60},
    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 3, 1},
    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1000, 700},
    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1000, 700},
    NO_MAPPING
  }
#endif
};
