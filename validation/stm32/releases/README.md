# Optional historical qualification package tooling

`validation/stm32/releases/` is a **gitignored working directory**, not a
mandatory evidence archive or retention registry. Previous HardRT versions
used it for optional `.tar.xz` bundles, but current releases record a concise
qualification summary instead.

After a full physical release qualification on the versioned source, publish
the tested SHA, board/configuration, date and PASS/FAIL totals. Generated raw
logs and optional local archives may be discarded after the run has been
verified. Do not commit them or promise retrieval of historical diagnostics.

When fresh measurements are needed, check out the relevant immutable tag in
a clean worktree with a supported STM32CubeH7 checkout and connected board:

```bash
git switch --detach X.Y.Z
./scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7 --clean-builds
```

This reproduces the **test procedure**, not necessarily identical timing
samples or the original historical evidence.

Legacy utilities `scripts/package_stm32_qualification.sh` and
`scripts/finalize_release.sh` may still produce local packages such as
`hardrt-stm32-qualification-X.Y.Z.tar.xz`, but they are not required to tag,
package or publish the GitHub software release.

See [release process](../../../docs/RELEASE_PROCESS.md) and
[hardware qualification policy](../../../docs/QUALIFICATION.md).
