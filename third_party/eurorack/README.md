Subset of Mutable Instruments' eurorack firmware and stmlib, by Emilie Gillet,
MIT licensed (see the header of each file and stmlib/LICENSE).

- eurorack f4cfa7697095a28d292bff758beb51deb49b5a86
- stmlib d18def816c51d1da0c108236928b2bbd25c17481

Files are verbatim except:
- rings/resources.{h,cc}: lut_sine is a pointer filled at init in SDRAM, and
  the unused lookup_table_table index is dropped.
