// nts3-mutable: stand-in for clouds/drivers/debug_pin.h. The original toggles
// an STM32 GPIO for profiling (its TEST branch, kept here, does nothing).
// granular_processor.cc includes it but only mentions TIC in a comment.

#ifndef CLOUDS_DRIVERS_DEBUG_PIN_H_
#define CLOUDS_DRIVERS_DEBUG_PIN_H_

namespace clouds {

class DebugPin {
 public:
  static void Init() { }
  static void High() { }
  static void Low() { }
};

#define TIC DebugPin::High();
#define TOC DebugPin::Low();

}  // namespace clouds

#endif  // CLOUDS_DRIVERS_DEBUG_PIN_H_
