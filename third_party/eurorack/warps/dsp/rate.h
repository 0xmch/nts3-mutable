// nts3-mutable addition: the sample rate Warps runs at. Upstream runs at
// 96 kHz; the NTS-3 Meta Mod unit runs it at 48 kHz (WARPS_SAMPLE_RATE=48000)
// to halve its CPU load. Sample-rate-aware code takes the rate from Init();
// the vocoder's filter bank table is generated per rate (resources.cc); and
// the per-sample one-pole constants tuned at 96 kHz go through
// WARPS_ONE_POLE, which keeps their time constants: c at 96 kHz is
// 1 - (1 - c)^2 at 48 kHz.

#ifndef WARPS_DSP_RATE_H_
#define WARPS_DSP_RATE_H_

#ifndef WARPS_SAMPLE_RATE
#define WARPS_SAMPLE_RATE 96000
#endif

#if WARPS_SAMPLE_RATE == 96000
#define WARPS_ONE_POLE(c) (c)
#elif WARPS_SAMPLE_RATE == 48000
#define WARPS_ONE_POLE(c) ((c) * (2.0f - (c)))
#else
#error "WARPS_SAMPLE_RATE must be 96000 or 48000"
#endif

#endif  // WARPS_DSP_RATE_H_
