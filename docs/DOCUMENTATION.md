# API documentation & Doxygen

HeaRTOS documents its public API directly in headers under `inc/` using Doxygen comment blocks.

## Where the docs live
- Public API: declarations in `inc/` headers include `/** ... */` blocks.
- Internal/static helpers in `.c` files are intentionally not documented unless promoted to public API.
- Tests and examples are not part of the generated API docs.

## Comment style guidelines
- Use `/** ... */` for Doxygen blocks on declarations.
- Include tags where appropriate:
  - `@brief` — a concise summary line
  - `@param` — one per parameter
  - `@return` — return value description
  - `@note` — important caveats or behavioral notes
  - `@code` … `@endcode` — for short usage snippets when behavior is non-obvious

## Minimal Doxygen generation
If you use Doxygen, a minimal configuration can generate browsable HTML docs from headers:

```bash
# from repo root; assuming doxygen is installed
mkdir -p docs/html
(
  echo "INPUT = inc/ cpp/";
  echo "EXTRACT_ALL = NO";
  echo "RECURSIVE = YES";
  echo "GENERATE_HTML = YES";
  echo "OUTPUT_DIRECTORY = docs";
) > Doxyfile.min

doxygen Doxyfile.min
# open docs/html/index.html
```

## Related references
- C API summary: docs/API_C.md
- Optional C++ wrapper: docs/CPP.md
