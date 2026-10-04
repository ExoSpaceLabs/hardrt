# Release process

HardRT uses `develop` as the integration/release-candidate branch and `main` as released history. Release tags use `X.Y.Z` without a `v` prefix.

The process intentionally separates three things:

1. **hardware qualification**, performed manually on one frozen source SHA;
2. **software staging**, performed automatically by `.github/workflows/release.yml` when the release tag is pushed;
3. **physical-evidence retention**, performed locally by `scripts/finalize_release.sh`.

This keeps hundreds of hardware logs out of Git while preserving the complete evidence set as one deterministic local archive. Publishing that archive to any hosting service is deliberately outside the finalizer.

## 1. Prepare and qualify the release candidate

Merge all target/build-affecting changes into `develop`, require hosted/cross-build/documentation CI to pass, and freeze the candidate SHA.

The normal Linux CI executes the hosted POSIX suite on native amd64 and native arm64 runners. The release workflow repeats the hosted release validation on both architectures before either POSIX package is accepted.

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

The permanent Release workflow then:

1. validates version/ref alignment;
2. executes the hosted POSIX validation suite natively on Linux amd64;
3. executes the same hosted POSIX validation suite natively on Linux arm64;
4. builds and validates an installed POSIX package separately on each architecture;
5. builds the Cortex-M package;
6. creates architecture-qualified deterministic archives and one `SHA256SUMS` file;
7. creates a **draft** GitHub Release containing the software assets.

The workflow does not publish a combined bundle. The POSIX install tree contains compiled architecture-specific objects, so its archive name must always identify the Linux architecture.

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

The local retention path is gitignored. The archive and checksum are retained evidence files, not tracked source files.

## 4. Finalize the release locally

After the release tag exists and the complete hardware qualification run has been retained, run:

```bash
./scripts/finalize_release.sh \
  X.Y.Z \
  validation/stm32/<UTC>_<short-sha>
```

Requirements:

- standard command-line tools and `git`;
- the `X.Y.Z` tag already exists;
- `origin/main` equals the release tag;
- `origin/develop` contains the release tag and may already be ahead;
- the retained hardware run is a complete unfiltered PASS.

Historical 0.5.1 is the sole exception. Its complete original run directory is no longer retained, and the later `release/0.5.1` staging branch was audited as an incomplete copy. When the supplied 0.5.1 run path is missing, the finalizer validates [QUALIFICATION_0_5_1.md](QUALIFICATION_0_5_1.md), the immutable qualified/release SHAs, and the qualification-diff guard. It does **not** fabricate a full archive from the partial staging data.

The finalizer has no hosting-service CLI/API dependency. It does not require an account login, inspect release pages, or upload assets.

The finalizer:

1. refreshes `main`, `develop`, and the release tag from `origin`;
2. validates tag/CMake version alignment;
3. packages the retained hardware evidence through `package_stm32_qualification.sh`;
4. validates the hardware-qualified SHA is an ancestor of the release tag;
5. runs `check_release_qualification_diff.py` between the qualified SHA and release/tag SHA;
6. verifies the generated xz archive, expected archive contents, and SHA-256 checksum locally;
7. reports finalization success without mutating remote branches.

Branch cleanup is deliberately **outside** the finalizer. After finalization, the release operator reviews and deletes temporary branches manually using the hosting UI or ordinary git. The finalizer never performs a remote push, never asks for branch-cleanup credentials, and never deletes refs.

## Existing local evidence and retries

The finalizer does not overwrite an existing qualification package by default. Rebuilding the same local archive/checksum requires:

```text
--force-package
```

Use that only when intentionally regenerating the retained evidence package. Release tags themselves are never moved or rewritten by the script.

## Historical 0.5.1 finalization

HardRT 0.5.1 predates the current release-evidence workflow. Its software release artifacts remain historical and are not rewritten by the local finalizer.

For 0.5.1, the finalizer validates the immutable git relationship between the qualified source, release tag, `main`, and current `develop`. If the original complete run directory is available, it is packaged normally. If it is missing, the finalizer instead verifies the committed [historical qualification record](QUALIFICATION_0_5_1.md), re-runs the post-qualification diff guard, and reports that no complete raw archive is retained.

The October 2026 cleanup audit established that `release/0.5.1` contains only a truncated staging copy: 78 raw files are recoverable, `qualification.md` is absent, and the gzip stream is incomplete. That branch is therefore not treated as full qualification evidence and can be retired after the historical record has been verified.

No hosting-service release modification is required to retire the old `release/0.5.1` branch.

## Canonical artifacts and retained evidence

The release workflow still produces the canonical software packages:

```text
hardrt-posix-linux-amd64-X.Y.Z.tar.gz
hardrt-posix-linux-arm64-X.Y.Z.tar.gz
hardrt-cortexm-X.Y.Z.tar.gz
SHA256SUMS
```

Those software artifacts are validated by CI and are independent of the local finalizer.

Physical qualification evidence is retained locally as:

```text
hardrt-stm32-qualification-X.Y.Z.tar.xz
hardrt-stm32-qualification-X.Y.Z.tar.xz.sha256
```

The Git repository contains neither the unpacked hardware logs nor the generated qualification archive. If the project later chooses to publish those files on a hosting service, that publication is separate from release finalization and does not change the qualified-source contract.
