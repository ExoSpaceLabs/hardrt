# Release process

HardRT uses `develop` as the integration/release-candidate branch and `main` as released history. Release tags use `X.Y.Z` without a `v` prefix.

The process intentionally separates three things:

1. **hardware qualification**, performed manually on one frozen source SHA;
2. **software publication**, performed automatically by `.github/workflows/release.yml` when the release tag is pushed;
3. **physical-evidence publication and branch cleanup**, performed by `scripts/finalize_release.sh` after the GitHub Release exists.

This keeps hundreds of hardware logs out of Git while still publishing the complete evidence set as one immutable release asset.

## 1. Prepare and qualify the release candidate

Merge all target/build-affecting changes into `develop`, require hosted/cross-build/documentation CI to pass, and freeze the candidate SHA.

Run the complete unfiltered STM32 qualification matrix:

```bash
./scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7 --clean-builds
```

Release evidence requires:

- board/OpenOCD probe PASS;
- 13/13 functional contracts PASS;
- 38/38 hardware benchmarks PASS;
- Overall PASS;
- clean tracked HardRT source recorded in `qualification.md`.

The runner writes the complete evidence tree under:

```text
validation/stm32/<UTC>_<short-sha>/
```

Do not commit that directory.

## 2. Promote and tag

Normally the hardware-qualified SHA is promoted unchanged to `main`. If release automation/documentation-only changes must follow qualification, they are allowed only under the narrow policy in [QUALIFICATION.md](QUALIFICATION.md) and must pass:

```bash
python3 scripts/check_release_qualification_diff.py <qualified-sha> <release-sha>
```

Before tagging, `main` and `develop` are aligned at the intended release commit.

Create and push the non-v-prefixed tag:

```bash
git tag X.Y.Z <release-sha>
git push origin X.Y.Z
```

The permanent Release workflow then validates version/ref alignment, builds the POSIX and Cortex-M install packages, creates reproducible archives and checksums, and publishes the GitHub Release.

## 3. Package physical qualification evidence

Package the selected passing hardware run locally:

```bash
./scripts/package_stm32_qualification.sh \
  X.Y.Z \
  validation/stm32/<UTC>_<short-sha>
```

The helper refuses partial, dirty, or failed reports. It requires the report to record:

- clean tracked source;
- the unfiltered full qualification mode;
- Overall PASS;
- a valid 40-character HardRT SHA available in the repository.

It creates a deterministic archive under:

```text
validation/stm32/releases/X.Y.Z/
```

with exactly two publishable files:

```text
hardrt-stm32-qualification-X.Y.Z.tar.xz
hardrt-stm32-qualification-X.Y.Z.tar.xz.sha256
```

The archive contains the original `qualification.md`, every raw build/OpenOCD/GDB log, and `PACKAGE_METADATA.txt` identifying the release version, qualified SHA, and source run directory.

The local retention path is gitignored. The archive and checksum are release assets, not tracked source files.

## 4. Finalize the GitHub Release

After the tag-driven Release workflow has published the software packages, run:

```bash
./scripts/finalize_release.sh \
  X.Y.Z \
  validation/stm32/<UTC>_<short-sha> \
  --cleanup-branches
```

Requirements:

- authenticated GitHub CLI (`gh auth login`);
- the `X.Y.Z` tag already exists;
- `origin/main` equals the release tag;
- `origin/develop` contains the release tag. It may already be ahead after development resumes;
- the corresponding GitHub Release already exists.

The finalizer:

1. refreshes `main`, `develop`, and the release tag;
2. validates tag/CMake version alignment;
3. validates the hardware-qualified SHA is an ancestor of the release tag;
4. runs `check_release_qualification_diff.py` between the hardware-qualified SHA and release/tag SHA;
5. packages the hardware evidence through `package_stm32_qualification.sh`;
6. uploads the `.tar.xz` archive and `.sha256` file with `gh release upload`;
7. downloads the published assets again and verifies the checksum;
8. only after successful publication, optionally deletes every remote branch except `main` and `develop`.

Branch deletion is deliberately behind `--cleanup-branches` and requires confirmation. For non-interactive use after the release state has been reviewed:

```bash
./scripts/finalize_release.sh \
  X.Y.Z \
  validation/stm32/<UTC>_<short-sha> \
  --cleanup-branches \
  --yes
```

The branch cleanup implements the repository policy that completed release work leaves only the two long-lived branches.

## Existing assets and retries

The finalizer is safe to rerun when the published qualification assets already match the local package. It downloads and compares same-named assets instead of blindly replacing them.

If a same-named release asset differs, finalization fails. Replacement requires the explicit option:

```text
--replace-assets
```

Use that only when intentionally correcting release evidence. Release tags themselves are never moved or rewritten by the script.

A local package can likewise be rebuilt only with:

```text
--force-package
```

## Canonical release assets

A completed release contains software assets generated by CI:

```text
hardrt-posix-X.Y.Z.tar.gz
hardrt-cortexm-X.Y.Z.tar.gz
hardrt-bundle-X.Y.Z.tar.gz
SHA256SUMS
```

and physical evidence generated locally:

```text
hardrt-stm32-qualification-X.Y.Z.tar.xz
hardrt-stm32-qualification-X.Y.Z.tar.xz.sha256
```

The Git repository contains neither the unpacked hardware logs nor the generated qualification archive. The GitHub Release is the publication boundary for those artifacts.
