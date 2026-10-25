# Release process

HardRT uses `develop` as the integration/release-candidate branch and `main` as released history. Application-dependent numerical mutex blocking claims require the independent evidence described in [MUTEX_BLOCKING_BUDGET.md](MUTEX_BLOCKING_BUDGET.md); 0.6 does not automatically certify application `H`/`D` budgets. Release tags use `X.Y.Z` without a `v` prefix.

The process intentionally separates three things:

1. **hardware qualification**, performed manually on one frozen source SHA;
2. **software staging**, performed after successful current-`main` CI by automatic `VERSION`-driven tagging and a directly invoked reusable `.github/workflows/release.yml`;
3. **physical-evidence retention**, performed locally by `scripts/finalize_release.sh`.

This keeps hundreds of hardware logs out of Git while preserving the complete evidence set as one deterministic local archive. Publishing that archive to any hosting service is deliberately outside the finalizer.

## 0.6 acceptance boundary

Per the approved [0.6 tracker](https://github.com/ExoSpaceLabs/hardrt/issues/91),
the numerical STM32H755 task-set mutex blocking/IRQ-interference proof is
reserved for the 0.7 timing phase (#53/#52/#54). Acceptance of 0.6 is
**conditional structural timing analysis** plus fully passing current
hardware functional and uncontended DWT diagnostic evidence, not a claim of
global numerical worst-case blocking. Nothing in that scope change relaxes
the final 16/52 frozen-SHA physical qualification gate.

## 1. Prepare and qualify the release candidate

Merge all target/build-affecting changes into `develop`, require hosted/cross-build/documentation CI to pass, and freeze the candidate SHA.

The normal Linux CI executes the hosted POSIX suite on native amd64 and native arm64 runners. The release workflow repeats the hosted release validation on both architectures before either POSIX package is accepted.

Run the complete unfiltered STM32 qualification matrix:

```bash
./scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7 --clean-builds
```

Release evidence requires:

- board/OpenOCD probe PASS;
- **16/16** functional contracts PASS;
- **52/52** benchmark metrics PASS (38 legacy images plus one 14-metric synchronization image);
- Overall PASS;
- clean tracked HardRT source recorded in `qualification.md`.

The runner writes the complete evidence tree under:

```text
validation/stm32/<UTC>_<short-sha>/
```

Do not commit that directory.

## 2. Promote and tag

Prepare root `VERSION` and release notes on `develop` **before** the final
hardware run. Promote through an ordinary reviewed `develop -> main` GitHub
PR merge; a fast-forward or direct push does **not** authorize automated
tagging. The release PR must contain these exact human attestation lines:

```text
Hardware-qualified SHA: <40-character develop commit SHA>
Hardware qualification: PASS
```

The operator must personally confirm the complete locally retained hardware
report and raw evidence, not infer a physical PASS from GitHub CI. The merge
will have a different SHA; release authorization requires:

```bash
python3 scripts/check_release_qualification_diff.py <qualified-develop-sha> <main-merge-sha>
```

Only approved documentation changes may differ. The guard fails on changes to
all other source/build/version/workflow/test inputs. After successful required
Linux, Signal stress and Documentation CI **on the current `main` SHA**, the
tagger checks the PR provenance and qualifications, creates non-v `VERSION`
tag exactly once, and **calls** the reusable packaging workflow. It refuses
stale CI, direct pushes, unrelated PRs, missing attestation, changed build inputs
and an existing version tag at a different SHA. Version changes on `develop`
do not trigger tagging.

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
- `origin/develop` contains the hardware-qualified `develop` commit; it need not contain the merge/tag commit;
- the retained hardware run is a complete unfiltered PASS.

Historical 0.5.1 is the sole exception. Its complete original run directory is no longer retained, and the later `release/0.5.1` staging branch was audited as an incomplete copy. When the supplied 0.5.1 run path is missing, the finalizer validates [QUALIFICATION_0_5_1.md](QUALIFICATION_0_5_1.md), the immutable qualified/release SHAs, and the qualification-diff guard. It does **not** fabricate a full archive from the partial staging data.

The finalizer has no hosting-service CLI/API dependency. It does not require an account login, inspect release pages, or upload assets.

The finalizer:

1. refreshes `main`, `develop`, and the release tag from `origin`;
2. validates tag/CMake version alignment;
3. packages the retained hardware evidence through `package_stm32_qualification.sh`;
4. validates the hardware-qualified SHA is an ancestor of the release tag;
5. runs `check_release_qualification_diff.py` to prove qualified build-input equivalence between the qualified SHA and distinct release/tag merge SHA;
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

For 0.5.1, the finalizer validates the immutable git relationship between the qualified source, release tag, `main`, and current `develop`. If the original complete run directory is available, it is packaged normally. If it is missing, the finalizer instead verifies the committed [historical qualification record](QUALIFICATION_0_5_1.md), applies the immutable 0.5.1 historical evidence record and version-specific qualification policy, and reports that no complete raw archive is retained.

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
