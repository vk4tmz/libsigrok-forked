# Hantek 1008C BURST, diagnostic ROLL, and official Scan acquisition

## Observed acquisition mechanisms

The Hantek 1008C currently has three observed acquisition/transfer mechanisms relevant
to this driver. Two are implemented in the production driver; the official Windows
Scan Mode transport is characterized in the Python protocol laboratory but is not yet
promoted to production.

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

**Diagnostic ROLL mode** is the existing continuous low-rate `A4 02 + C7/C8` path.
The device reports the number of available bytes with `C7` and the host drains them
with `C8`. There is no 4K sweep boundary defining the trace. This behaves more like a
strip-chart recorder: new samples continuously arrive and are appended to the timeline.
It is a useful validated low-rate transport, but it is not the official Hantek Windows
application's Scan Mode.

**Official Scan Mode** is used by the Windows application from 500 ms/div (`A3=1A`)
and slower. It keeps `A4 01` and transfers a continuous byte stream using `C9/CA`.
The production driver exposes the independently validated `A3=1A` through `22`
settings as 800, 400, 200, 80, 40, 20, 8, 4, and 2 Sa/s respectively. The two words in each 4-byte row
are emitted as successive CH1 observations in their evidence-backed temporal order.

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
| 2, 4, 8, 20, 40, 80, 200, 400, 800 Sa/s | official Scan |
| 1003, 2006 Sa/s | Trigger-region C7/C8 |
| 800 kSa/s | BURST |
| 2.4 MSa/s | BURST |

The current exposed one-channel samplerate list is:

`2, 4, 8, 20, 40, 80, 200, 400, 800, 1003, 2006, 800000, 2400000 Sa/s`

The older diagnostic ROLL rates `1, 5, 9, 23, 50, 100, 201, 401 Sa/s`
are intentionally not advertised to PulseView. Their internal mappings remain
available as preserved protocol history, but the public list presents the
official Scan cadence for every validated timebase from 500 ms/div onward.

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

## Sample limits in BURST mode

`SR_CONF_LIMIT_SAMPLES` is an aggregate session limit, but a Hantek BURST is a
physical 4K acquisition frame. The driver therefore never truncates the final
physical BURST frame to hit a non-multiple-of-4000 sample limit exactly.

Examples:

| Requested sample limit | BURST frames delivered | Samples delivered |
|---:|---:|---:|
| 1,000 | 1 | 4,000 |
| 4,000 | 1 | 4,000 |
| 5,000 | 2 | 8,000 |
| 10,000 | 3 | 12,000 |
| 20,000 | 5 | 20,000 |

This preserves physical sweep integrity and prevents a frontend from displaying a
shortened final sweep. ROLL mode is continuous and may stop exactly at the requested
sample count. For BURST acquisition, `limit_frames` is the natural control when an
exact number of sweeps is required.


### PulseView sample-count normalization

PulseView uses `SR_CONF_LIMIT_SAMPLES` not only as a stop condition but also as the
capture size it expects to retain/display. In BURST mode the driver therefore rounds
a non-zero requested sample limit **up to the next complete 4000-sample frame at
configuration time**, and reports that effective value back through
`SR_CONF_LIMIT_SAMPLES`.

Examples: 5K -> 8K, 10K -> 12K, 20K -> 20K.

This is in addition to never truncating packets at the session bus. ROLL mode keeps
the exact requested sample limit because it is a true continuous stream.

## Official Windows timebase / trigger evidence

Targeted USBPcap captures of the official Hantek application on 2026-08-29 establish a distinction that the production driver must preserve:

| UI time/div | A3 | Official regime | transfer family |
|---:|---:|---|---|
| 100 ms | `0x18` | Trigger | C6/A6 |
| 200 ms | `0x19` | Trigger | C6/A6 |
| 500 ms | `0x1A` | Scan Mode | C9/CA |
| 1 s | `0x1B` | Scan Mode | C9/CA |

The official application still uses `A4 01` in the observed C9/CA Scan Mode. Therefore
the driver's existing low-rate `A4 02 + C7/C8` ROLL implementation is a different
mechanism and must not be renamed or treated as official Scan Mode.


### Startup A3 synchronization

The Python C7/C8 ROLL reference path performs the complete startup/final
configuration using the selected low-rate A3 value and then sends that same A3
again immediately before the `A4 02` ROLL arm sequence.  libsigrok now mirrors
that validated flow.  It no longer substitutes `A3=0F` during full startup when
a low-rate ROLL samplerate was selected.

This is an existing-flow synchronization fix; it does not enable official
`C9/CA` Scan Mode and it does not change BURST sequencing.

### Linux C9/CA Scan validation

The Python protocol-laboratory path now independently reproduces the official
`A4 01 + C9/CA` Scan transport on Linux. The following framing rules are supported by
repeated hardware captures:

- In steady state, `C9` values from 1 through 64 specify the valid prefix length of the
  following fixed 64-byte `CA` response. Bytes after that prefix are zero padding.
- Occasional startup `C9` values greater than 64 have different/unknown semantics.
  They are quarantined and are not interpreted as a FIFO byte count or sample data.
- USB `CA` transaction boundaries are transport boundaries only. A stateful framer
  carries 0 through 3 bytes between responses and emits only complete neutral 4-byte
  candidate rows. Capture-end carry is preserved verbatim; it is never padded or
  discarded.
- A candidate row consists of two little-endian 16-bit values, currently named
  `word0` and `word1`. Their exact temporal/device semantics are deliberately not yet
  assigned.

Three-point cadence validation with the same CH1 configuration gives:

| A3 | Official time/div | Candidate 4-byte rows/s | Capture-end carry |
|---:|---:|---:|---:|
| `0x1A` | 500 ms/div | 397.194 | 0 B |
| `0x1B` | 1 s/div | 198.649 | 0 B |
| `0x1C` | 2 s/div | 99.173 | 2 B |

The approximately 400/200/100 row/s progression closely matches the independently
measured `C7/C8` low-rate cadence at the same A3 values (about 401/201/100 per second).
This strongly supports 4-byte logical row framing for the tested CH1-only Scan stream,
while not proving what one row should mean as a libsigrok analogue sample.

For an `A3=1C` capture of the onboard test signal, `word0` spanned 1998..2202 and
`word1` 1999..2201 ADC-like counts. The mean absolute within-row difference was about
1.096 counts and the maximum was 66 counts. This strongly indicates related analogue
observations rather than unrelated metadata, but is not sufficient to choose one word,
average them, or emit two samples per row in the production driver.

Accordingly, the proven C9/CA transport/framing findings are documented here, but the
production acquisition code intentionally remains unchanged until `word0`/`word1`
semantics and the correct libsigrok sample representation are established in the
waveform-agnostic Python reference path.

The same official captures establish these trigger controls:

- `AB hi lo`: vertical trigger threshold, big-endian 16-bit ADC-domain value.
- `AC [u16] [u24] [u24]`: horizontal acquisition-window / trigger-position data; the u24 pair partitions the total horizontal window.
- `C1 00 xx`: Edge-trigger slope/polarity. A dedicated `+/-` toggle capture produces alternating `C1 00 01` / `C1 00 00`. Do not label numeric polarity orientation until transition ordering is unambiguous.
- Trigger Sweep `Auto / Normal / Single`: no dedicated new configuration opcode was proven; observed behaviour is consistent with acquisition/re-arm policy. Do not invent a sweep selector byte.

These are protocol findings only. They do not authorize waveform-specific cleanup, smoothing, interpolation, or changes to canonical direct-ADC reconstruction.


### AC A/B result at A3=11

A controlled Python BURST A/B test compared the driver's historical final AC
value `0,1,1401` with the official Windows A3=11 value `0,1,5001`, while
holding CH1/A2/A3/A4 and the acquisition sequence constant.  Both settings
reached ready state and returned the same physical acquisition geometry:
buffer 02 empty, buffer 03 8000 bytes, 125 complete A6 packets and no trailing
bytes.  This confirms that the official value is accepted but does not prove
that samplerate-driven BURST should reproduce the official UI's AC mapping.
The production AC value therefore remains unchanged pending evidence of a
functional requirement.

### Scan word0/word1 adjacency evidence

Waveform-agnostic adjacency analysis of the preserved A3=1C official C9/CA
Scan capture compares `word0[n] -> word1[n]` with the row-boundary transition
`word1[n] -> word0[n+1]`.  Mean absolute deltas are about 1.096 and 1.000 ADC
counts respectively, with very similar <=1-count populations and correlations
of about 0.9985 and 0.9991.  There is therefore no observed numerical
continuity break at the 4-byte row boundary.

This is evidence consistent with the two words forming consecutive ADC-like
observations in an interleaved stream, rather than one analogue value plus
unrelated metadata.  It is not yet a production decoding rule.  In particular,
the driver must not emit two samples per row, average the words, or otherwise
reinterpret C7/C8 or C9/CA until the result is repeated across rates and its
effective timing is reconciled with independent sample-rate measurements.

### Official Scan temporal-order cross-rate evidence (2026-08-29)

Controlled Python protocol-lab tests now provide stronger evidence for the
4-byte official `A4 01 + C9/CA` Scan row.  With CH1 grounded, A3=1A, 1B and 1C
showed no persistent separation between word0 and word1: all alternating and
same-position adjacency classes collapsed to the same small ADC-noise regime.
With CH1 driven by a 20 Hz sine, `word0[n] -> word1[n]` and
`word1[n] -> word0[n+1]` had essentially identical mean absolute deltas at all
three rates, while `word0[n] -> word0[n+1]` and `word1[n] -> word1[n+1]` were
approximately twice as large.

Measured Scan row rates and alternating-boundary results were:

- A3=1A: 398.170 rows/s; within 2.3285 counts, across 2.3306 counts.
- A3=1B: 198.993 rows/s; within 4.4623 counts, across 4.5069 counts.
- A3=1C: 99.124 rows/s; within 8.8992 counts, across 8.8384 counts.

This is strong evidence consistent with a temporal Scan sequence
`word0[n], word1[n], word0[n+1], word1[n+1], ...`, implying approximately two
ADC-like observations per 4-byte Scan row.  Do not transfer that interpretation
to the production C7/C8 ROLL decoder yet: the C7/C8 transport must be tested
independently with both raw row words preserved.  Production sample-rate
mapping and emission remain unchanged pending that cross-check.

### C7/C8 ROLL word-role cross-check (2026-08-29)

The C9/CA Scan temporal interpretation was cross-checked directly against the
separate diagnostic `A4 02 + C7/C8` ROLL transport rather than being transferred
by analogy.  Raw 4-byte ROLL rows were preserved with both 16-bit positions and
analysed with the same neutral adjacency metrics.

With CH1 driven by a 20 Hz sine, word0 followed the changing analogue input
while word1 remained nearly static.  Mean same-position row deltas were:

- A3=1A (401 rows/s): word0 30.0287 counts, word1 0.93375 counts.
- A3=1B (201 rows/s): word0 59.6008 counts, word1 1.19399 counts.
- A3=1C (100 rows/s): word0 113.07 counts, word1 1.6775 counts.

At all three rates the alternating word0/word1 boundaries were separated by
about 259 counts, rather than showing the equal adjacent temporal spacing seen
in C9/CA Scan.  A grounded-CH1 A3=1A control then reduced the word0 row-step to
0.6927 count and word1 row-step to 0.6458 count, while the two row positions
remained separated by about 249 counts.

This is strong evidence that C7/C8 word0 and word1 have different semantics.
The existing production ROLL path therefore remains correct to emit word0 only;
word1 must not be treated as a second consecutive CH1 sample and the historical
ROLL samplerates must not be doubled.  Word1 remains deliberately unnamed
beyond "word1" because the current evidence does not establish whether it is
metadata, status, another analogue quantity, or something else.

The result also confirms that C9/CA Scan and C7/C8 ROLL can share 4-byte row
geometry while carrying different row semantics.  Production acquisition is
unchanged by this documentation update.

### Python Scan reference-path promotion (2026-08-29)

The evidence-backed Python protocol/reference path now decodes official
`A4 01 + C9/CA` Scan rows as the temporal CH1 sequence
`word0[n], word1[n], word0[n+1], word1[n+1], ...` and reports an observation
cadence of two CH1 observations per measured 4-byte row.  This promotion is
confined to the Python Scan reference path; it does not change the libsigrok
production driver.

The decode is structural only and performs no waveform-specific cleanup,
smoothing, thresholding, interpolation, averaging, integration or detrending.
A validation-only Python reference-tone analyzer is used to check the existing
20 Hz ATR2x-USB Scan captures against the resulting observation cadence before
any C9/CA production implementation is considered here.

Do not transfer this two-observation rule to C7/C8 ROLL.  Direct C7/C8 testing
has already established different word semantics, and production ROLL remains
word0-only at the existing historical samplerates.

### Initial production C9/CA Scan implementation (2026-08-29)

The production driver now has a deliberately narrow official Scan path for the
nine independently validated settings only:

| libsigrok samplerate | A3 | Official time/div | Transport |
|---:|---:|---:|---|
| 800 Sa/s | `0x1A` | 500 ms/div | `A4 01 + C9/CA` Scan |
| 400 Sa/s | `0x1B` | 1 s/div | `A4 01 + C9/CA` Scan |
| 200 Sa/s | `0x1C` | 2 s/div | `A4 01 + C9/CA` Scan |
| 80 Sa/s | `0x1D` | 5 s/div | `A4 01 + C9/CA` Scan |
| 40 Sa/s | `0x1E` | 10 s/div | `A4 01 + C9/CA` Scan |
| 20 Sa/s | `0x1F` | 20 s/div | `A4 01 + C9/CA` Scan |
| 8 Sa/s | `0x20` | 50 s/div | `A4 01 + C9/CA` Scan |
| 4 Sa/s | `0x21` | 100 s/div | `A4 01 + C9/CA` Scan |
| 2 Sa/s | `0x22` | 200 s/div | `A4 01 + C9/CA` Scan |

The advertised Scan rates are nominal observation rates.  Repeated host-side
measurements saw about 398/199/99/40 four-byte rows per second, and the validated
row interpretation provides two temporally ordered CH1 observations per row.
A 20 Hz reference-tone check recovered 19.92/19.90/19.82 Hz across A3=1A/1B/1C
and 19.843/19.846 Hz in two A3=1D Python captures when the interleaved stream was
interpreted at twice the measured row cadence.

Scan startup mirrors the evidence-backed Python reference sequence: selected
A3, official Scan AC `0,1,1`, `F3`, `A4 01`, `E4 01`, `E6 01`, `C0`, about
1.87 seconds of repeated `F3/A5` observation, then `C2`.  Steady-state reads use
`F3`, `A5 5A`, `C9`, and exactly one fixed 64-byte `CA` transaction when C9 is
non-zero.  C9 values 1..64 select the valid CA prefix.  Values above 64 remain
quarantined as unresolved startup/oversize conditions and are not emitted.

The Scan decoder keeps a 0..3 byte carry across CA transaction boundaries and
emits complete rows only.  A complete row is decoded structurally as
`word0, word1`, producing the temporal sequence
`word0[n], word1[n], word0[n+1], word1[n+1], ...`.  No averaging, smoothing,
interpolation, thresholding, detrending or waveform-specific processing is
performed.

The existing diagnostic `A4 02 + C7/C8` ROLL implementation remains preserved
internally at its historical word0-only samplerates. Those legacy rates are not
advertised to PulseView because official Scan is now the public path throughout
the validated 500 ms/div-and-slower region. Direct 20 Hz and grounded-input
testing established that C7/C8 has different word semantics, so its historical
rates are not reinterpreted or doubled.
Official Scan takes precedence at the public 2 Sa/s setting, so the historical
`A3=21` ROLL mapping is no longer selected there; the ROLL implementation itself
is unchanged.

### Production hardware validation

Official C9/CA Scan has passed real-hardware validation at all nine currently
exposed production settings:

| Rate | A3 | Official time/div | Validation |
|---:|---:|---:|---|
| 800 Sa/s | `0x1A` | 500 ms/div | `sigrok-cli` and PulseView |
| 400 Sa/s | `0x1B` | 1 s/div | `sigrok-cli` and PulseView |
| 200 Sa/s | `0x1C` | 2 s/div | `sigrok-cli` and PulseView |
| 80 Sa/s | `0x1D` | 5 s/div | `sigrok-cli` and PulseView |
| 40 Sa/s | `0x1E` | 10 s/div | `sigrok-cli` and PulseView |
| 20 Sa/s | `0x1F` | 20 s/div | `sigrok-cli` and PulseView |
| 8 Sa/s | `0x20` | 50 s/div | `sigrok-cli` and PulseView |
| 4 Sa/s | `0x21` | 100 s/div | `sigrok-cli` and PulseView |
| 2 Sa/s | `0x22` | 200 s/div | `sigrok-cli` and PulseView |

Each check used the ATR2x-USB audio output: a 20 Hz reference for A3=1A through
1D and a 1 Hz reference for A3=1E through 21. Both were reproduced at the
expected cadence. The checks validate the existing waveform-agnostic production
decode; they do not introduce smoothing, averaging, interpolation, thresholding,
or other waveform-specific reconstruction.

At A3=1D, two Python reference captures measured 79.374 and 79.383 observations/s
and recovered 19.843 and 19.846 Hz. A grounded control measured 79.151
observations/s with a four-count span. The production `sigrok-cli` capture then
delivered exactly 800 samples at the advertised 80 Sa/s and recovered exactly
20.000 Hz; PulseView showed the expected sparse approximately four-sample-per-cycle
trace.

The Python-first A3=1E/1F/20/21 batch used a 1 Hz ATR2x-USB reference and
measured steady observation cadences of 40.1103, 20.1001, 8.10128, and 4.10395
observations/s after excluding each profile's queued startup packet. Grounded
controls had only 2--4 count spans, valid zero padding, and no unhandled C9
event. Production `sigrok-cli` delivered exactly 800, 400, 160, and 80 samples
at 40, 20, 8, and 4 Sa/s and recovered 1.000000, 0.999702, 1.000000, and
1.000000 Hz without altering sample values. PulseView showed clean sine waves
at 40 and 20 Sa/s, a coarse trace at 8 Sa/s, and an expected repeating roughly
triangular four-samples-per-cycle trace at 4 Sa/s. This validates cadence and
periodicity at the lowest rate, not high-fidelity waveform shape.

At A3=22, the Python-first capture measured 1.899987 observations/s and
recovered a 0.2 Hz ATR2x-USB reference at 0.189999 Hz with an exact median
period of 10 observations. Its grounded control had a four-count span and
0.7115-count population standard deviation; padding was valid and no oversize
C9 event occurred. Production `sigrok-cli` then delivered exactly 120 samples
at 2 Sa/s and recovered 0.200244 Hz. PulseView showed the expected roughly
ten-sample-per-cycle sine after its display scale was manually changed from the
initial 20 V/div to 2 V/div. Display scaling did not alter acquisition data.

### Ultra-slow Python protocol/cadence validation: A3=23 through A3=28 (2026-08-29)

The Python protocol/reference path has now exercised every remaining official
Windows Scan profile through A3=28. A3=23 used a 600-second capture; A3=24..28
used 600, 900, 1500, 2500, and 5000 seconds respectively. CH1 was connected to
the ATR2x-USB audio output. These are protocol/cadence validations, not yet
production libsigrok or PulseView validations.

| A3 | Official time/div | Nominal observation rate | Measured observation rate | Complete rows | Final carry | Oversize CA |
|---:|---:|---:|---:|---:|---:|---:|
| `0x23` | 500 s/div | 0.8 Sa/s | 0.790 Sa/s | 237 | 2 B | 0 |
| `0x24` | 1000 s/div | 0.4 Sa/s | 0.390 Sa/s | 117 | 2 B | 0 |
| `0x25` | 2000 s/div | 0.2 Sa/s | 0.193 Sa/s | 87 | 2 B | 0 |
| `0x26` | 5000 s/div | 0.08 Sa/s | 0.076 Sa/s | 57 | 2 B | 0 |
| `0x27` | 10000 s/div | 0.04 Sa/s | 0.038 Sa/s | 47 | 2 B | 0 |
| `0x28` | 20000 s/div | 0.02 Sa/s | 0.019 Sa/s | 47 | 2 B | 0 |

Every capture retained the same C9/CA framing semantics: the first non-empty CA
transaction supplied a two-byte valid prefix, steady transactions supplied
four-byte valid prefixes, padding was zero, and no oversize CA transaction was
observed. At A3=28, for example, one 2-byte prefix plus 47 four-byte prefixes
produced exactly 190 bytes = 95 16-bit observations = 47 complete rows plus a
two-byte capture-end carry.

Regular four-byte CA arrivals were approximately 2.5, 5, 10, 25, 50, and 100
seconds apart for A3=23..28, corresponding to approximately 1.25, 2.5, 5, 12.5,
25, and 50 seconds per observation. This supports one continuous official C9/CA
Scan family from A3=1A through A3=28 and confirms that the final profiles are
genuinely fractional-Hz acquisitions.

The attempted 0.001 Hz ATR2x-USB reference is **not** treated as waveform
validation. The audio output did not produce a meaningful near-DC reference;
for example, A3=28 occupied only ADC codes 2027..2037 (10-count span, population
standard deviation approximately 2.34 counts) during the 5000-second run. The
run therefore establishes framing and cadence only. Its quiet sequential
differences are consistent with the already-established Scan temporal ordering
and do not show the distinct word split previously observed in diagnostic
C7/C8 ROLL.

A grounded-CH1 control remains to be run for A3=23..28. Production Scan remains
limited to A3=1A..22 while the correct libsigrok representation of rates below
1 Hz is resolved. Do not round or otherwise fake these profiles as integer
`SR_CONF_SAMPLERATE` values; investigate timebase and/or sample-interval
semantics instead. Existing C7/C8 ROLL and C6/A6 BURST behaviour remain
unchanged.
