# Goodix 27c6:5381 porting notes

These notes document the evidence used for the initial experimental port. They
do not contain firmware, biometric data, or a device-specific PSK.

## Tested hardware

- Laptop: Dell G5 15 5587
- USB ID: `27c6:5381`
- USB data interface: 1
- Bulk endpoints: `0x03` OUT and `0x81` IN
- Maximum bulk packet size: 64 bytes
- Firmware observed across resets: `GF5288_HTSEC_APP_10020` and
  `GF3208_HTSEC_APP_10020`; both exact strings are present in Dell's PID5381
  module
- Chip ID: `0x002202a0`
- OTP reply length: 32 bytes

The interface, endpoints, packet size and wrapless protocol match the existing
53x5 driver. Ping, firmware query, reset, chip-ID query, OTP read and PSK-hash
read all complete successfully.

The fallback OTP fields yield `delta_fdt = 0`. That value is retained for
finger-event validation, but open-time baseline stabilization uses a minimum
tolerance of 3. Three is the smallest value produced by the normal OTP formula;
without this open-only floor, ordinary sensor noise made consecutive baseline
reads require bit-for-bit equality and caused intermittent device-open failures.
The 5381 open path also spaces retries by 100 ms and permits up to ten retries,
because back-to-back reads immediately after enrollment were only about 10 ms
apart and repeatedly sampled the same unsettled state. Failures report the
observed maximum delta without logging raw FDT or biometric data.

## Calibration evidence

Dell publishes Goodix driver `KP8ND`, version `3.31.30.110 A08`, for the Dell G5
15 5587. Its SHA-256 is
`701572c668467ef14bad3e4c6d21ae726bf0764a1093cd2c1951fafe200f9b6a`, matching
the checksum on Dell's download page.

Static comparison of the package's separate `PID5381` and `PID5395` modules
shows that both contain a ten-entry configuration table. The existing driver
used entry 9, while the `0x2202` Milan F path selects entry 0. It also uses a
fixed two-byte image-prepare payload (`01 00`) instead of the four-byte Milan
FNHV payload. The `PID5381` module uses the same OTP offsets and formulas for
`tcode`, FDT deltas and DAC values. Its path accepts the chip OTP without the
single-byte hash check used by the reverse-engineered `5395` utility. This is
consistent with the test device: its 32-byte OTP does not validate under the
`5395` hash rule, while the calibration fields select the driver's documented
fallback values.

## PSK safety

The test device's factory per-machine PSK was replaced with the known all-zero
PSK after explicit opt-in. The write was re-read and its SHA-256 verified before
GTLS was attempted. That write is persistent and can affect later Windows
compatibility. For `27c6:5381`, the driver therefore stops after its read-only
probe unless full initialization is explicitly enabled. A second independent
opt-in is required before a PSK write.

No firmware write was performed.

## Live validation

With configuration entry 0 and the Milan F image request, the device completed
GTLS, configuration upload, FDT calibration and a full encrypted reference
capture. Reference and live captures passed both the encrypted-payload HMAC and
the GEA image CRC. After the four CRC bytes are removed, the decrypted image is
exactly 14,256 bytes: 9,504 packed 12-bit samples with no additional image
header.

The prototype interprets each four-sample group as a 2x2 spatial
block. For a group `q0..q3`, the output positions are top-left `q0`, top-right
`q3`, bottom-left `q1` and bottom-right `q2`. The resulting 108x88 image holds
two adjacent halves interpreted as readings of the same 54x88 sensor area. The 5381 path averages
those readings and expands the result horizontally to preserve the dimensions
expected by the feature extractor. Enrollment also retains the initial
no-finger reference because this device reports finger-up before a finger has
necessarily left the sensing area.

This image-layout interpretation remains a prototype assumption, not confirmed
native parity. HMAC and CRC success establish payload integrity, not the spatial
interpretation or correctness of the final image-processing algorithm.

An eight-stage enrollment completed with 173 to 228 SIGFM keypoints per sample.
All 28 enrollment-sample pairs exceeded the matching threshold of 150; the
minimum score was 240. A subsequent live verification matched with a best score
of 778. A first verification attempt scored 40, showing that finger placement
is still sensitive. These results validate the full path on one reader and one
finger, but do not establish false-accept behavior or accuracy across devices.

### Reference continuity across fprintd's duplicate check

Subsequent testing exposed an enrollment lifecycle bug. fprintd performs an
IDENTIFY duplicate check before starting ENROLL, advertising nine stages for
this eight-sample driver. The 5381 can report finger-up while the finger is
still touching the glass. Capturing a new reference at ENROLL start therefore
can incorporate that finger into the reference used for all eight samples.

Two fresh verification captures scored 29,974 against each other with the
existing preprocessing, while scoring only 72 and 35 against the old enrolled
print. Only 10 of the old enrollment's 28 sample-pair comparisons reached the
150 gate. These measurements narrowed the failure to the enrollment path;
they did not justify changing the matching threshold or image geometry.

For the 5381 only, a successful IDENTIFY now retains its pre-touch reference
for an immediately following ENROLL on the same open device. VERIFY, a new
IDENTIFY, errors and device close discard the reference; enrollment also
discards it if hardware reinitialization is required. Other USB IDs keep their
existing behavior. A direct enrollment without a preceding duplicate check
still captures its own initial reference.

The corrected live enrollment logged reuse of the pre-touch reference, and
the newly enrolled index finger subsequently verified successfully. Prints
enrolled with a contaminated reference need re-enrollment. Temporary diagnostic
variants were evaluated in memory, without storing raw images, and removed
from the final implementation.

A subsequent verification after restarting fprintd with the diagnostic-free
build returned `verify-no-match`. The reference-lifecycle correction alone
therefore does not yet establish reliable authentication. An unregistered-finger
rejection test and repeated successful verifications remain to be confirmed.

The driver host tests and libfprint's three core unit suites (device, SSM,
assembling) passed. The optional metainfo validation failed because the upstream
issue-tracker URL was unreachable; the other driver replay tests were skipped
in this build without introspection. The installed library was checked against
the final build by SHA-256, and the temporary diagnostic service override was
removed.

The SIGFM angle checks also clamp floating-point dot/cross ratios before inverse
trigonometric operations and compare relative angles with an absolute 0.10-rad
tolerance. These fixes follow the LGPL-licensed corrections in the
[`goodix-27c6-55a4-fingerprint-linux`](https://github.com/Hydrogell/goodix-27c6-55a4-fingerprint-linux)
implementation.

No raw biometric image or enrollment template is stored in this repository.
