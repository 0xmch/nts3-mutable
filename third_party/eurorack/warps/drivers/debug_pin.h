// nts3-mutable: stand-in for warps/drivers/debug_pin.h. The original toggles
// an STM32 GPIO for profiling (its TEST branch, kept here, does nothing).
// modulator.cc includes it but only mentions TIC in a comment.

#ifndef WARPS_DRIVERS_DEBUG_PIN_H_
#define WARPS_DRIVERS_DEBUG_PIN_H_

namespace warps {

class DebugPin {
 public:
  static void Init() { }
  static void High() { }
  static void Low() { }
};

#define TIC DebugPin::High();
#define TOC DebugPin::Low();

}  // namespace warps

#endif  // WARPS_DRIVERS_DEBUG_PIN_H_
