# Goodix 5381: experimental branch and Milan contribution assessment

Assessment date: 2026-09-09.

## Scope and evidence

`experimental-27c6-5381` preserves the AndyHazz/SIGFM prototype and its commit
history. It is shared for review and hardware/protocol observations, not as a
merge-ready implementation for seaweeduk's native Milan stack.

The latest correction keeps the pre-touch reference across fprintd's
IDENTIFY-to-ENROLL duplicate check for the 5381. A corrected enrollment and
successful verification were observed, but a later verification after restarting
the diagnostic-free build returned `verify-no-match`. Repeated recognition,
unregistered-finger rejection, cold-start and suspend/resume validation remain
open. The matching threshold was not lowered to obtain a successful result.

On 2026-09-09, `bash scripts/run-tests.sh` passed all four host test binaries:
crypto, FDT policy, SIGFM extraction and SIGFM matching. These tests do not
exercise physical sensor timing or establish authentication reliability. No
new sensor operation or system installation was performed for publication.

See [porting notes](27c6-5381-porting.md) for observations and earlier tests.
Only source and documentation are included in this contribution; no fingerprint
images, templates, device credentials or vendor binaries are added.

## Reviewed upstream starting point

- [Issue 205](https://github.com/seaweeduk/goodix53x5-libfprint/issues/205)
- [`milan-dev` handoff at f8b912b](https://github.com/seaweeduk/goodix53x5-libfprint/blob/f8b912bbbe97c95a09c5a3c082380b26cf1d0c7b/PROFILE0-SUPPORT-SCOPE.md)

That branch replaces SIGFM with native Milan processing and reorganizes the
device code. Its profile-0 handoff describes distinct USB calibration,
reference/capture, coating/preprocessing, extraction, matching and persistence
requirements. Applying the SIGFM branch wholesale would not implement those
contracts or preserve the current profile-9 stack.

Our observations came from Dell package KP8ND 3.31.30.110 A08. The maintainer's
research uses DLL pair 2.0.310.900. In particular, the prototype's two-byte
image request and the handoff's four-byte request must be reconciled rather
than silently treated as interchangeable. The prototype's image fusion and
eight-stage enrollment are not evidence of native Milan behavior.

## Proposed integration sequence

1. Discuss a bounded profile-0 implementation and validation proposal in the
   issue, using this prototype as evidence and the upstream handoff as the base.
2. Establish isolated profile-0 native-oracle execution and comparison policy.
3. Introduce verified profile/subtype selection and ownership boundaries with
   profile 9 remaining the only enabled implementation; preserve its behavior.
4. Port verified calibration and capture semantics in a small change, then
   implement the separate preprocessing and algorithm differences. Do not
   copy speculative image reconstruction or relax compatibility gates.
5. Integrate profile-aware print/cache and diagnostic/parity handling; preserve
   existing profile-9 prints and evidence contracts.
6. Validate native parity and physical lifecycle behavior before enabling 5381.

A draft implementation PR should follow a reviewable, tested adaptation on the
target stack. This assessment does not claim such an adaptation is complete.
The current publication intentionally leaves the installed prototype untouched.
