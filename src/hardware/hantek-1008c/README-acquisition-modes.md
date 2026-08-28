# Hantek 1008C BURST and ROLL acquisition modes

## Two different acquisition models

The Hantek 1008C has two fundamentally different acquisition models.

**BURST mode** is a finite, triggered/swept acquisition. The scope is armed, a block of
samples is captured into hardware memory, the completed frame is transferred to the
host, and the scope is then re-armed for another acquisition.

This is the digital equivalent of a traditional triggered CRT oscilloscope. A classic
CRT scope waits for a trigger, sweeps the beam from left to right, then retraces/resets
before the next sweep. The retrace interval is not part of the displayed waveform.
Likewise, the Hantek's time between BURST frames is dead time: it was not sampled and
must never be filled with invented samples.

With one active channel the Hantek has a 4K-sample BURST memory. Repeated 4K frames can
therefore look "live" in a frontend, just as repeated CRT sweeps look continuous to the
eye, but the frames are still separate acquisitions.

**ROLL mode** is continuous low-rate acquisition. The device reports the number of
available bytes with `C7` and the host drains them with `C8`. There is no 4K sweep
boundary defining the trace. This behaves more like a strip-chart recorder: new
samples continuously arrive and are appended to the timeline.

## Sample-rate and waveform guidance

The "comfortable maximum frequency" values below are deliberately more conservative
than Nyquist. They are intended for a useful oscilloscope display, not merely for
detecting that a signal exists.

- Sine: approximately 10 samples/cycle or better.
- Square: approximately 20 samples/cycle or better.
- The Hantek's 100 kHz analog bandwidth also limits usable waveform content.
- For square waves, the table caps the fundamental near 20 kHz so useful low-order
  harmonics can still fit inside the 100 kHz analog front end.

These are engineering guidance values, not manufacturer guarantees.

## Relevant BURST A3 / timebase / rate mapping

A3 is the horizontal acquisition selector. Its nominal time/div value and the actual
ADC sample rate are related but are not identical concepts; adjacent A3 values can
share the same measured sample rate.

| A3 | Nominal time/div | Validated one-channel rate | Notes |
|---:|---:|---:|---|
| `0x0E` | 50 us/div | 2.4 MSa/s | validated Python mapping |
| `0x0F` | 100 us/div | 2.4 MSa/s | canonical 2.4 MSa/s selector |
| `0x10` | 200 us/div | 800 kSa/s | validated Python mapping |
| `0x11` | 500 us/div | 800 kSa/s | canonical 800 kSa/s selector |

A full 4K frame at 800 kSa/s contains 5.000 ms of sampled time. A ~1 kHz square wave
therefore produces about five complete cycles in one frame, which has been confirmed
on hardware.

A full 4K frame at 2.4 MSa/s contains about 1.667 ms of sampled time.

## ROLL rate mapping

For one active channel:

| A3 | Rate | Approx. sample interval |
|---:|---:|---:|
| `0x22` | 1 Sa/s | 1.000 s |
| `0x21` | 2 Sa/s | 0.500 s |
| `0x20` | 5 Sa/s | 0.200 s |
| `0x1F` | 9 Sa/s | 0.111 s |
| `0x1E` | 23 Sa/s | 43.5 ms |
| `0x1D` | 50 Sa/s | 20.0 ms |
| `0x1C` | 100 Sa/s | 10.0 ms |
| `0x1B` | 201 Sa/s | 4.98 ms |
| `0x1A` | 401 Sa/s | 2.49 ms |
| `0x19` | 1.003 kSa/s | 0.997 ms |
| `0x18` | 2.006 kSa/s | 0.499 ms |

The 2.006 kSa/s, 1.003 kSa/s, 401 Sa/s and 201 Sa/s points have been checked on
hardware with a 50 Hz sine. The observed progression was approximately 40, 20, 8 and
4 samples/cycle respectively.

## Comfortable waveform-frequency table

### BURST

| Rate | 4K sampled span | Comfortable sine | Comfortable square |
|---:|---:|---:|---:|
| 800 kSa/s | 5.000 ms | ~80 kHz | ~20 kHz |
| 2.4 MSa/s | 1.667 ms | ~100 kHz* | ~20 kHz* |

`*` limited primarily by the 100 kHz analog front end rather than sample rate.

### ROLL

| Rate | Comfortable sine | Comfortable square |
|---:|---:|---:|
| 1 Sa/s | ~0.1 Hz | ~0.05 Hz |
| 2 Sa/s | ~0.2 Hz | ~0.1 Hz |
| 5 Sa/s | ~0.5 Hz | ~0.25 Hz |
| 9 Sa/s | ~0.9 Hz | ~0.45 Hz |
| 23 Sa/s | ~2.3 Hz | ~1.15 Hz |
| 50 Sa/s | ~5 Hz | ~2.5 Hz |
| 100 Sa/s | ~10 Hz | ~5 Hz |
| 201 Sa/s | ~20 Hz | ~10 Hz |
| 401 Sa/s | ~40 Hz | ~20 Hz |
| 1.003 kSa/s | ~100 Hz | ~50 Hz |
| 2.006 kSa/s | ~200 Hz | ~100 Hz |

## Interpretation rules

1. Never join BURST frames by inventing samples for acquisition dead time.
2. Never alter samples based on knowing the expected waveform shape.
3. Test sine and square waves are validation signals, not reconstruction hints.
4. Nyquist is not the same as a comfortable oscilloscope display limit.
5. Any proven acquisition/protocol change must be checked in both the Python reference
   implementation and the libsigrok production driver.

## libsigrok implementation status

This document belongs to the libsigrok Hantek 1008C production driver.

The driver automatically selects acquisition mode from the requested samplerate.

| Requested rate | Driver mode |
|---:|---|
| 1 Sa/s through 2.006 kSa/s | ROLL |
| 800 kSa/s | BURST |
| 2.4 MSa/s | BURST |

The current exposed one-channel samplerate list is:

`1, 2, 5, 9, 23, 50, 100, 201, 401, 1003, 2006, 800000, 2400000 Sa/s`

### BURST representation in sigrok

Each physical BURST acquisition is represented as a real sigrok frame:

`FRAME_BEGIN -> ANALOG samples -> FRAME_END`

Successive 4K frames are separate sweeps. They are intentionally not made contiguous
with synthetic gap samples. PulseView can refresh successive frames quickly enough to
look like a moving/live oscilloscope, but 800 kSa/s and 2.4 MSa/s remain BURST mode.

Canonical BURST selectors are:

| Rate | A3 |
|---:|---:|
| 800 kSa/s | `0x11` |
| 2.4 MSa/s | `0x0F` |

The 800 kSa/s path requires the selected A3 value to be established during the full
direct-ADC initialization. Re-sending A3 immediately before each `A4 01` arm was found
to disturb acquisition state and corrupt the latter part of the frame. With A3 moved
into the initialization path, the full 4K / 5 ms frame is clean.

### ROLL representation in sigrok

ROLL uses the continuous `C7`/`C8` transport. C7 reports the number of bytes currently
ready and repeated C8 transactions drain those bytes. ROLL samples are sent as a
continuous analog stream and are not wrapped in BURST frame markers.

The 2.006 kSa/s, 1.003 kSa/s, 401 Sa/s and 201 Sa/s rates were validated using a 50 Hz
sine signal and produced the expected samples-per-cycle progression.
