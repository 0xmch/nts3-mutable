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

// Texture unit header: Mutable Instruments Clouds for the NTS-3 kaoss pad kit.
// Derived from the logue SDK dummy-genericfx template (BSD 3-Clause, KORG).
// TEXTURE_MODE picks the Clouds playback mode, and with it the unit's name, ID
// and Density default (Density is the diffusion amount outside granular mode).
// TEXTURE_QUALITY 3 (granular only) is Clouds' mono 8-bit lo-fi quality.

#include "unit_genericfx.h"

#ifndef TEXTURE_MODE
#define TEXTURE_MODE 0
#endif

#ifndef TEXTURE_QUALITY
#define TEXTURE_QUALITY 0
#endif

#if TEXTURE_MODE == 0 && TEXTURE_QUALITY == 3
#define TEXTURE_NAME "Texture Lo-Fi"
#define TEXTURE_UNIT_ID 0x7U
#define TEXTURE_DENSITY_INIT 700
#elif TEXTURE_QUALITY != 0
#error "only the granular mode has a lo-fi unit"
#elif TEXTURE_MODE == 0
#define TEXTURE_NAME "Texture"
#define TEXTURE_UNIT_ID 0x2U
#define TEXTURE_DENSITY_INIT 700
#elif TEXTURE_MODE == 1
#define TEXTURE_NAME "Texture Stretch"
#define TEXTURE_UNIT_ID 0x3U
#define TEXTURE_DENSITY_INIT 0
#elif TEXTURE_MODE == 2
#define TEXTURE_NAME "Texture Loop"
#define TEXTURE_UNIT_ID 0x4U
#define TEXTURE_DENSITY_INIT 0
#else
#error "TEXTURE_MODE must be 0 (granular), 1 (stretch) or 2 (looping delay)"
#endif

const __unit_header genericfx_unit_header_t unit_header = {
  .common = {
    .header_size = sizeof(genericfx_unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_genericfx,
    .api = UNIT_API_VERSION,
    .dev_id = 0x30784D43U,  // '0xMC'
    .unit_id = TEXTURE_UNIT_ID,
    .version = 0x00010000U,
    .name = TEXTURE_NAME,
    .num_params = 8,

    .params = {
      // Format: min, max, center, default, type, frac. bits, frac. mode, <reserved>, name
      {0, 1000, 0, 0, k_unit_param_type_percent, 1, 1, 0, {"Position"}},
      {0, 1000, 0, 500, k_unit_param_type_percent, 1, 1, 0, {"Size"}},
      {-1000, 1000, 0, 0, k_unit_param_type_drywet, 1, 1, 0, {"Mix"}},
      {-24, 24, 0, 0, k_unit_param_type_semi, 0, 0, 0, {"Pitch"}},
      {0, 1000, 0, TEXTURE_DENSITY_INIT, k_unit_param_type_percent, 1, 1, 0, {"Density"}},
      {0, 1000, 0, 500, k_unit_param_type_percent, 1, 1, 0, {"Texture"}},
      {0, 1000, 0, 0, k_unit_param_type_percent, 1, 1, 0, {"Feedback"}},
      {0, 1000, 0, 250, k_unit_param_type_percent, 1, 1, 0, {"Reverb"}}},
  },
  .default_mappings = {
    // Format: assign, curve, curve polarity, min, max, default value
    {k_genericfx_param_assign_x, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1000, 0},
    {k_genericfx_param_assign_y, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1000, 500},
    {k_genericfx_param_assign_depth, k_genericfx_curve_linear, k_genericfx_curve_unipolar, -1000, 1000, 0},
    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, -24, 24, 0},
    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1000, TEXTURE_DENSITY_INIT},
    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1000, 500},
    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1000, 0},
    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1000, 250}
  }
};
