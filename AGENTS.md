# When writing code

- Use functions/procedures instead of objects wherever possible
  - No OOP
- Prefer simple, straight-line code to complex abstractions
- Run tests with a timeout to avoid hanging tests during development.
- Avoid altering tests unless you know the test is testing the wrong thing.
- Three pre-merge baselines for any change to `src/` or `include/`:
  `./build.sh` (88/0), `./build.sh --tsan` (86/2 known-and-safe),
  `./build.sh --wasm` (Emscripten compile check; skipped if `emcc`
  isn't installed). See `AUDIT.md` for rationale.
- Prefer snake_case for values and functions. Prefer PascalCase for types.
- If there is a DESIGN.md, upon completion of an entire prd.json, update DESIGN.md to show what was completed. Be very concise about things already implemented.

## When writing C

- Prefer arenas for memory management. This require organizing allocations by lifetime.
- Use data-oriented design for fast code with good cache locality.
- Follow the philosophy of Mike Acton, Casey Muratori, and Ryan Fleury.
- Prefer simple, direct code and flat data.
- Avoid the standard library.
- If the code is segfaulting or seeing memory corruption, it is not acceptable.
- If test code finds a memory handling issue in the implementation, that should be fixed in the implementation.
- Use typedefs to avoid unnecessary use of the `struct` keyword.
- Tests should make use of the memory tracking helpers in `test/test_helpers.h`.
  - This means that implementations either need to use the Allocator from `platform/platform.h` or they need to use macro-based polymorphism to allow the allocator to be set statically.
- Use helpers from `platform/platform.h` where appropriate.
