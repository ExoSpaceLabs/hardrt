# API documentation & Doxygen

HardRT documents its public API directly in headers under `inc/` and the C++ wrapper under `cpp/` using Doxygen comment blocks.

## Where the docs live
- Public C API: declarations in `inc/`
- Public C++ wrapper: declarations in `cpp/hardrtpp.hpp`
- Feature guides and behavioral notes: `docs/`
- Internal/static helpers in `.c` files are intentionally not documented unless promoted to public API
- Tests and examples are not part of the generated API docs

## Public API surface to keep documented

At this point the documented public surface includes:
- core task/scheduler/time API
- semaphores
- mutexes
- queues
- optional C++ wrappers for the same primitives

When one of these changes, the corresponding docs under `docs/` should be updated in the same branch.

## Comment style guidelines
- Use `/** ... */` for Doxygen blocks on declarations.
- Include tags where appropriate:
  - `@brief` — concise summary
  - `@param` — one per parameter
  - `@return` — return value description
  - `@note` — caveats or behavioral notes
  - `@code` … `@endcode` — short usage snippets when behavior is non-obvious

## Minimal Doxygen generation

```bash
mkdir -p docs/html
(
  echo "INPUT = inc/ cpp/";
  echo "EXTRACT_ALL = NO";
  echo "RECURSIVE = YES";
  echo "GENERATE_HTML = YES";
  echo "OUTPUT_DIRECTORY = docs";
) > Doxyfile.min

doxygen Doxyfile.min
```

Then open `docs/html/index.html`.

## Related references
- C API summary: `docs/API_C.md`
- C++ wrapper guide: `docs/CPP.md`
- Semaphores: `docs/SEMAPHORES.md`
- Mutexes: `docs/MUTEXES.md`
- Queues: `docs/QUEUES.md`
