Subset of Mutable Instruments' eurorack firmware and stmlib, by Emilie Gillet,
MIT licensed (see the header of each file and stmlib/LICENSE).

- eurorack f4cfa7697095a28d292bff758beb51deb49b5a86
- stmlib d18def816c51d1da0c108236928b2bbd25c17481

Files are verbatim except:
- rings/resources.{h,cc}, clouds/resources.{h,cc}: the big sine and window
  tables are pointers filled at init in SDRAM, and the unused table indexes
  are dropped.
- clouds/dsp/window.h: restores `done_ = false;` in Window::Start(). On
  upstream master since 2023-03-13 (merge d1d8839c11 of fbb53ba24b and
  0e3756fd56, which each dropped one copy of a duplicated line) Clouds' stretch
  mode never starts a window and stays silent. The 2015 firmware has the line.
- clouds/drivers/debug_pin.h: an empty stand-in for the STM32 profiling pin.
