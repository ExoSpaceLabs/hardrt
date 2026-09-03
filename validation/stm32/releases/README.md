# Release qualification packages

Keep **one directory per released version**:

```text
validation/stm32/releases/v0.5.0/
validation/stm32/releases/v0.6.0/
```

Each directory is produced by `scripts/promote_stm32_qualification.sh` from one complete passing local run. Do not add ad-hoc timestamped runs here.
