# HardRT 0.5.1 hardware qualification record

This file is the durable historical qualification record for HardRT 0.5.1. It records the physical qualification result that was completed and audited at release time, together with the later-discovered retention limitation.

```text
record_format=hardrt-historical-qualification-v1
release=0.5.1
run=20260907T220115Z_1802c763
qualified_sha=1802c76392203b5e93eb73285c96ad0be30d4474
release_sha=43dddffbfacdf2b1ed01b33940ac8f32a14d334d
board=NUCLEO-H755ZI-Q
core=CM7
functional=13/13
benchmarks=38/38
overall=PASS
full_raw_archive_retained=no
audited_raw_logs=156
partial_staging_raw_files=78
partial_staging_has_qualification_report=no
partial_staging_head=2b452f79d4429375d4c200e7ecb241ee02c1f439
```

## Qualification result

The release-candidate run used the NUCLEO-H755ZI-Q CM7 target with clean tracked HardRT source at `1802c76392203b5e93eb73285c96ad0be30d4474`.

At release time the recorded result was:

- board/OpenOCD probe: PASS;
- functional contracts: **13 / 13 PASS**;
- hardware benchmarks: **38 / 38 PASS**;
- external TIM2 tick contract using `hrt_tick_from_isr()`: PASS with SysTick disabled;
- overall result: **PASS**.

The evidence audit performed during release work recorded 52 referenced evidence patterns across 156 raw logs, with no failure signatures in the audited set. The final release/tag commit is `43dddffbfacdf2b1ed01b33940ac8f32a14d334d`; the allowed post-qualification difference is restricted by `scripts/check_release_qualification_diff.py`.

## Retention limitation

The complete original run directory is no longer retained on the workstation used for current cleanup.

A later temporary branch, `release/0.5.1`, attempted to stage the evidence as five base64 chunks. That staged copy was audited during October 2026 cleanup and is **incomplete**:

- the reconstructed compressed stream does not reach gzip EOF;
- the recoverable tar prefix contains 80 entries;
- only 78 raw evidence files are present;
- `qualification.md` is absent.

Therefore the staged branch is **not** a complete physical qualification archive and must never be represented as one. HardRT does not synthesize missing raw evidence or generate a canonical `hardrt-stm32-qualification-0.5.1.tar.xz` from that partial copy.

This historical record preserves the qualification outcome and exact source/release identities while explicitly recording that the full raw archive was not successfully retained. Releases after 0.5.1 remain subject to the normal rule: missing complete qualification evidence is a finalization failure.
