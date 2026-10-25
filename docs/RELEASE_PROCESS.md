# Release process

HardRT uses `develop` as the integration/release-candidate branch and `main` as released history. Application-dependent numerical mutex blocking claims require the independent evidence described in [MUTEX_BLOCKING_BUDGET.md](MUTEX_BLOCKING_BUDGET.md); 0.6 does not automatically certify application `H`/`D` budgets. Release tags use `X.Y.Z` without a `v` prefix.

The release handoff is **fully automated after the hardware-qualified source has
been merged to `main`**. The operator runs hardware tests before promotion.
After that, root `VERSION` and successful current-`main` CI drive the
immutable tag, native amd64/arm64 and Cortex-M packages, checksums and the
**published** GitHub Release, without a second CLI or manual publication step.

Physical raw evidence remains retained off-repository, because GitHub-hosted
runners cannot access files stored only on an operator's machine. Optional
archive packaging is available below, but it is **not a release-publication
dependency**. CI must not claim it physically repeated the board qualification.

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

## 2. Merge to main: automatic tag and publication

Before promotion, confirm the full qualification report and record the exact
source SHA with the release review. Update `VERSION` and the corresponding
`RELEASE_NOTES.md` section. Merge the qualified source into `main`.

No post-merge operator step is required. The automatic workflow:

1. Waits for **Linux**, **Signal stress** and **Documentation** push CI to all
   pass on the same *current* `main` commit.
2. Reads root `VERSION` and validates its matching release-note section.
3. If a newer non-`v` tag does not yet exist, creates the immutable
   `X.Y.Z` tag on that `main` commit. Old tags are never moved.
4. Builds and tests the native amd64 and native arm64 POSIX packages,
   Cortex-M package and `SHA256SUMS`; any failure blocks publication.
5. **Publishes** the GitHub Release automatically. It does **not** leave a
   draft waiting for a human to click Publish.
6. If a tagged version already has a draft, verifies its four software
   assets and checksums and publishes that draft automatically. A version
   already published is an idempotent no-op.

Normal post-release maintenance can advance `main` without rewriting the
existing tag or creating another release. Publishing the *next* version
requires changing `VERSION` to a newer value; it does not depend on the
merge shape, PR text, or a separate finalization command. The hardware
qualification occurs **before** merging and is not silently replaced by CI.

The workflow never publishes from `develop`, a pull request, stale CI, or a
partially passing CI matrix. Because the uploaded raw hardware evidence is
not present in the repository, automatic publication includes the software
release assets, not an invented physical test archive.

## 3. Optional local physical-evidence archival

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

## 4. Optional local evidence verification

If the complete original local hardware run is retained, the operator may
invoke the archival helper. This is **not** needed to publish a release:

```bash
./scripts/finalize_release.sh X.Y.Z validation/stm32/<UTC>_<short-sha>
```

The script verifies the existing immutable tag, its ancestry on current
`main`, source equivalence and report completeness, then validates the
generated archive and checksum. It never changes GitHub Releases, tags, or
branches. Unlike the obsolete equality guard, it continues to work after
`main` advances past the tagged release.

The historical `0.5.1` exception still validates its exact audited SHAs
and reports missing complete raw evidence rather than fabricating it.

Branch cleanup is deliberately **outside** the finalizer. Keep `main`,
`develop`, tags and unique-commit branches; delete obsolete fully merged
branches only after verifying their ancestry.

## Existing local evidence and retries

The finalizer does not overwrite an existing qualification package by default. Rebuilding the same local archive/checksum requires:

```text
--force-package
```

Use that only when intentionally regenerating the retained evidence package. Release tags themselves are never moved or rewritten by the script.

## Historical 0.5.1 finalization

HardRT 0.5.1 predates the current release-evidence workflow. Its software release artifacts remain historical and are not rewritten by the local finalizer.

For 0.5.1, the finalizer validates the immutable historical tag SHA, that the tag remains on the **ancestry** of current `main` (which correctly advances for newer releases), and the qualified source ancestry in `develop`. For every release the immutable tag must remain on the ancestry of current `main`; tag equality is not required after subsequent merges. If the original complete run directory is available, it is packaged normally. If it is missing, the finalizer instead verifies the committed [historical qualification record](QUALIFICATION_0_5_1.md), applies the immutable 0.5.1 historical evidence record and version-specific qualification policy, and reports that no complete raw archive is retained.

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

The Git repository contains neither the unpacked hardware logs nor the generated qualification archive. Optional publication of operator-retained physical logs is independent of the automatic software-release publication and does not change the qualified-source contract.
