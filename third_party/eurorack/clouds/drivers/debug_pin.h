// Copyright 2014 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
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
