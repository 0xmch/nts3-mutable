// Probe unit header. Derived from the logue SDK dummy-genericfx template
// (BSD 3-Clause, KORG).

#include "unit_genericfx.h"

#define REPORT(name) {0, 32767, 0, 0, k_unit_param_type_none, 0, 0, 0, {name}}
#define NO_MAPPING {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 0, 0}

const __unit_header genericfx_unit_header_t unit_header = {
  .common = {
    .header_size = sizeof(genericfx_unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_genericfx,
    .api = UNIT_API_VERSION,
    .dev_id = 0x30784D43U,  // '0xMC'
    .unit_id = 0x7FU,
    .version = 0x00010000U,
    .name = "Probe",
    .num_params = 8,
    .params = {
      REPORT("Frames"),
      REPORT("Calls"),
      REPORT("Armed"),
      REPORT("Counter"),
      REPORT("Max cycles"),
      REPORT("Period"),
      REPORT("Load"),
      REPORT("Set"),
    },
  },
  .default_mappings = {
    NO_MAPPING, NO_MAPPING, NO_MAPPING, NO_MAPPING,
    NO_MAPPING, NO_MAPPING, NO_MAPPING, NO_MAPPING,
  },
};
