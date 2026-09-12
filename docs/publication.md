# Experimental 5381 publication

The source snapshot on this branch was published using GitHub's web editor.
Its publication commits differ from the seven original local contribution
commits; this is not a native Git push of that history.

The original series is provided in
[`experimental-27c6-5381.mbox`](experimental-27c6-5381.mbox), exported with
`git format-patch --stdout`. It preserves the patches, commit messages and
author metadata. Apply it to the original base in a separate checkout:

```sh
git switch -c replay-5381 309d4c6999a1cdce172c1ca1ee81387b5078d38f
git am /path/to/experimental-27c6-5381.mbox
```

Reapplying patches normally creates new commit hashes. The source tree before
these publication-only files corresponds to local tip
`4a9be69b922025947e1fa7c16c68220202f3b325`.

Original contribution series, oldest first:

1. `be924b8` Add guarded experimental support for Goodix 5381
2. `1fd1a0b` Use Milan F protocol for Goodix 5381
3. `5566213` Complete image processing for Goodix 5381
4. `dba33ba` Document Goodix 5381 implementation and setup
5. `9b042bd` Tolerate 5381 open-time FDT noise
6. `a2562ff` Let 5381 FDT baseline settle between retries
7. `4a9be69` Preserve 5381 enrollment reference and document experimental limits

See the [integration assessment](5381-milan-contribution.md) and
[current observations](27c6-5381-porting.md) for the evidence boundary.
This remains a SIGFM prototype with incomplete reliability validation, not
an implementation of the native Milan profile-0 contracts.
