<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/logo/logo_white_fg_trans_bg_mini.png">
  <source media="(prefers-color-scheme: light)" srcset="docs/logo/logo_black_fg_white_bg_mini.png">
  <img alt="The Goose programming language" src="docs/logo/logo_black_fg_white_bg_mini.png">
</picture>

# The Goose Programming Language

A memory-safe systems language with no heap allocator, garbage collector, or
lifetime annotations. In this repository's benchmarks, Goose runs faster than
C++ and Rust while using less memory.

[Tutorial](docs/tutorial.md) · [Reference](docs/reference.md) ·
[Specification](docs/goose_spec.md) · [Command line](docs/command_line.md) ·
[Packages](docs/packages.md) ·
[Samples](samples/README.md) · [Benchmarks](bench/results.md) ·
[Standard library](docs/stdlib.md)

Goose has syntax familiar to C and Rust programmers, but **there is no heap**.
Every dynamic value lives inline on data stacks managed by the compiler.
Growth advances a pointer, and memory is freed when its scope ends. The
language's type and lifetime rules make this memory model safe to use.

[![Discord Chat](https://img.shields.io/badge/chat-discord-blue)](https://discord.gg/397eZdbnsR)
[![Twitter Follow](https://img.shields.io/twitter/follow/wvo.svg?style=social)](https://twitter.com/wvo)

## Experimental 32-Bit & Embedded/Wasm Support (`--m32`)

> **Note:** This repository includes an experimental 32-bit cross-compilation mode (`--m32`) that allows running the Goose compiler on a 64-bit host while emitting standalone, MMU-less 32-bit C code targeting bare-metal/RTOS microcontrollers (such as the Zephyr `qemu_cortex_m3` ARM Cortex-M3 target) and 32-bit WebAssembly (`wasm32-unknown-wasi` and Emscripten).

### Summary of Changes

1. **32-Bit Target Layout (`GS_TARGET_32=1`)**:
   - **32-bit slice and header lengths (`gs_len_t`)**: Defines `typedef int32_t gs_len_t;` (and `typedef gs_len_t gs_ulen_t GS_ALIGN1;`) under `GS_TARGET_32=1`. Slice structs `sl_T { T *data; gs_len_t len; }` and resizable headers `gs_rhdr { uint8_t *base; gs_len_t len; }` shrink from 16 bytes to **8 bytes**. In the Goose surface language, `.len` remains `i64` (widened on read, narrowed via `gs_len_t` on write) so existing Goose code and the standard library require zero source changes.
   - **4-byte pointer sizing in codegen**: `CodeGen::PtrSize()` returns `4` instead of `8` under `--m32`, ensuring compile-time layout calculations (`FixedSize`, `PadAlign`, `EmitCoreTypes`) match 32-bit target ABIs (`ILP32`). The generated C always states the width it was laid out for (`#define GS_TARGET_32 0|1`), and `runtime.h` refuses a C compiler of the other width with `#error`; size assertions on `void *` and `gs_rhdr` (`gs_assert_ptr_size`, `gs_assert_rhdr_size`) check the packed layout in the program and in any host that includes its `--header`.
   - **Reduced native C call-frame threshold**: `CodeGen::NativeValueLimit()` lowers the threshold at which fixed-size aggregates (`LargeFixedOnStack`) spill from the native C call stack to a Goose data stack from `4096` bytes to **`256` bytes**, protecting small RTOS/MCU thread call stacks (typically 1–4 KB).
2. **Static `.bss` Data Stacks with Heterogeneous Sizing (`GS_STATIC_STACKS=1`)**:
   - Replaces virtual-memory `mmap` + `PROT_NONE` guard pages with a single 8-byte-aligned static `.bss` buffer (`gs_main_stack_arena`) carved into per-stack regions at startup via `gs_static_stacks_setup(...)`.
   - **Per-slot size propagation (`BoundStacks`)**: The compiler tracks whether each data-stack slot across the call graph is used only for bounded `FixedLocal` values or for unbounded resizable growth. Bounded slots receive their exact 8-byte-aligned compile-time size in `gs_main_stack_sizes[]`, while unbounded dynamic stacks receive `GS_STACK_RESERVE`.
   - **Worker threads**: When `GS_NEED_THREADS=1`, main-thread stacks use the static `.bss` arena and spawned worker threads allocate their stack regions on the C heap (`malloc`/`free`) with the same software limit checks.
3. **Software Data-Stack Overflow Checks & 32-Bit Codegen/Runtime Optimizations**:
   - Each `gs_stack` carries `{ uint8_t *top; uint8_t *limit; }` under `GS_STATIC_STACKS`.
   - `cachetops` and construction-region `@@gs` top-pointer caching are disabled under `--m32` so `stk->top` is always live. Every data-stack write or bump calls `gs_check_space(stk, n)` first, which compares the remaining room against the `int64_t` count in 64 bits (so a count past 4 GB fails rather than passing truncated; a constant count folds to one 32-bit comparison) and reports a clean runtime stack-overflow error via `gs_stack_overflow(...)` without relying on `SIGSEGV`/`SIGBUS` handlers. The arena's total is checked by the C preprocessor against `SIZE_MAX`, so a `GS_STACK_RESERVE` the regions would wrap at is a compile error, and the runtime refuses to carve a region past the arena.
   - **32-bit indexing, pointer scaling, and narrow integer ops**: Under `--m32`, array indexing and bounds checks use 32-bit `GS_IDX32`, pointer offset scaling casts indices to `ptrdiff_t` before multiplying by `sizeof(elem)`, and checked arithmetic/division on `i8`/`i16`/`i32`/`u8`/`u16`/`u32` use 32-bit helpers (`gs_sdiv32`, `gs_udiv32`, etc.) to avoid software `__aeabi_ldivmod` calls on 32-bit MCUs.
   - **32-bit word copy/compare and integer formatting**: `gs_memcpy` and `gs_memeq` use 4-byte `uint32_t` word loops on 32-bit targets, and `gs_fmt_u64`/`gs_fmt_digits` switch to 32-bit division once the value fits in `uint32_t`.
   - **Relative reference (`T&<u16>`, `T&<u32>`) range checks**: unchanged from 64-bit mode, and as the spec has them (§3.9): a self-relative offset is signed, so the check is left out only where a root spans at most half the width's range -- `GS_STACK_RESERVE <= 2 GB` for `u32`, `<= 32 KB` for `u16`. With the default 64 KB stacks the `u16` check therefore stays in; `--stack-reserve 32K` or less elides it.
4. **Target-Agnostic `libc` Runtime (`GS_FREESTANDING`, `GS_NEED_OS` & `GS_NEED_THREADS` Gating)**:
   - When a program does not import `os` or spawn threads, the generated C depends only on standard ISO C `libc` (`<stdint.h>`, `<stddef.h>`, `<stdlib.h>`, `<string.h>`, `<stdio.h>`, `<math.h>`, `<limits.h>`, `<stdarg.h>`), with zero target-specific `#ifdef`s (`__ZEPHYR__`, `__ARM_EABI__`, or `__wasi__`) in the Goose compiler or runtime.
   - **`GS_FREESTANDING`** separates "no operating system layer" from "32-bit": it leaves out `dlopen` (`gs_dl_bind` panics if a program needs a dynamic extern library), reports one hardware thread, locks no `stdio` stream, lists no directories, skips `fchmod` in `write_file_atomic`, uses `setjmp` rather than `_setjmp`, keeps the scalar form of the vector helpers (`GS_VEC 0`), and shrinks `GS_OS_PATH_MAX` from `4096` to `256` bytes for small C call stacks. `--m32` defines it as `1` (under `#ifndef`, so `-DGS_FREESTANDING=0` on the compile line restores the POSIX layer for a 32-bit Linux); `GS_TARGET_32` itself only decides layout and the 32-bit arithmetic paths.
   - `<setjmp.h>` is included only when `GS_NEED_THREADS=1` (for `thread_spawn_contained`), allowing WASI targets to compile without experimental Wasm exception-handling proposals.
   - `gs_os_read_file`, `gs_os_read_stdin`, and `gs_os_read_stdin_max` read directly into `out.stk->top` (bounded by `out.stk->limit`) instead of allocating a 64 KB `uint8_t buf[65536]` on the C call stack.
   - Target-specific entry points and shims (Zephyr's `main(void)` calling the program's entry point, which the build names with `-DGS_MAIN=gs_app_main`; ARM semihosting `_exit`; and `clock_gettime`/`nanosleep` over `k_cycle_get_64`/`k_msleep`) live entirely in [`zephyr/harness.c`](zephyr/harness.c).

---

### Compiler Command-Line Arguments for 32-Bit Mode

* **`--m32`**:
  Enables 32-bit target code generation (`GS_TARGET_32=1`), static `.bss` data stacks (`GS_STATIC_STACKS=1`) and the freestanding runtime (`GS_FREESTANDING=1`).
  - Takes `-o <file.c>` (or `check`): the C is for another machine, so `run`, `build` and `--jit` refuse it. With `-o`, `--m32` automatically implies **`--standalone`**, embedding the required runtime headers directly into the single generated `.c` file for cross-compilation.
* **`--stack-reserve <size>`** (interaction with `--m32`):
  - In standard 64-bit mode, `--stack-reserve` defaults to `2G` (`1G` under sanitizers) and accepts sizes from `64K` (one OS guard page) to `256T`.
  - In **`--m32`** mode, `--stack-reserve` defaults to **`64K` (`65536` bytes)** per dynamic data stack (with a `0`-byte guard gap) and accepts any size from **`64` bytes** up to **`2G`** (with optional `K`, `M`, or `G` suffixes, e.g., `--stack-reserve 4K` for small MCUs or `--stack-reserve 128M` for memory-intensive benchmarks on Wasm32).
  - You can also override `GS_STACK_RESERVE` at C compile time via `-DGS_STACK_RESERVE=<bytes>` when compiling a generated `.c` file.
* **`--stacks`** (interaction with `--m32`):
  Reports the static data-stack and global-stack count for the main program and any workers using the `--m32` default (`64 KB reserved per stack, 0 bytes guard gap`) or the configured `--stack-reserve`.

```bash
# Inspect data-stack counts in 32-bit mode:
./build/goose --m32 --stacks --check samples/01_tour.goose

# Emit standalone 32-bit C with a default 64 KB reserve per dynamic stack:
./build/goose --m32 samples/01_tour.goose -o tour_m32.c

# Emit standalone 32-bit C with a custom 4 KB reserve per dynamic stack (for 64 KB SRAM MCUs):
./build/goose --m32 --stack-reserve 4K samples/01_tour.goose -o tour_cm3.c
```

---

### Building and Running with the Nix Flake (`flake.nix`)

The repository includes a [`flake.nix`](flake.nix) development environment that provides:
- Host build tools (`cmake`, `ninja`, `clang`)
- 32-bit WebAssembly toolchain and runner (`wasm32-unknown-wasi-clang`, `lld` / `wasm-ld`, `wasmtime`, `emscripten`)
- Pre-configured **Zephyr RTOS v3.7.0** workspace (`ZEPHYR_BASE`, `cmsis` module, `west`, `dtc`, `gperf`), 32-bit ARM Cortex-M3 toolchain (`arm-none-eabi-gcc`), and QEMU emulator (`qemu-system-arm`)

#### 1. Enter the Nix Dev Environment and Build the Compiler

```bash
nix develop path:.
cmake -B build -GNinja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

#### 2. Compile and Run on 32-Bit WebAssembly (`wasm32-unknown-wasi` + `wasmtime`)

```bash
# 1. Generate 32-bit standalone C from a Goose sample or benchmark
./build/goose --m32 samples/01_tour.goose -o /tmp/01_tour.c

# 2. Cross-compile to 32-bit WASI WebAssembly
wasm32-unknown-wasi-clang -O2 \
  -D_WASI_EMULATED_PROCESS_CLOCKS -D_WASI_EMULATED_SIGNAL \
  /tmp/01_tour.c \
  -lwasi-emulated-process-clocks -lwasi-emulated-signal -lm \
  -o /tmp/01_tour.wasm

# 3. Run in Wasmtime (pass --dir=. when the program reads local files)
wasmtime run --dir=. /tmp/01_tour.wasm
```

For benchmarks or samples that allocate multi-megabyte datasets, pass `--stack-reserve` (e.g., `--stack-reserve 16M` or `--stack-reserve 128M`):

```bash
./build/goose --m32 --stack-reserve 128M bench/goose/graph.goose -o /tmp/graph.c
wasm32-unknown-wasi-clang -O2 \
  -D_WASI_EMULATED_PROCESS_CLOCKS -D_WASI_EMULATED_SIGNAL \
  /tmp/graph.c \
  -lwasi-emulated-process-clocks -lwasi-emulated-signal -lm \
  -o /tmp/graph.wasm
wasmtime run /tmp/graph.wasm
```

#### 3. Compile and Run on Zephyr RTOS `qemu_cortex_m3` (64 KB SRAM Limit)

The repository includes a ready-to-build Zephyr RTOS application in [`zephyr/`](zephyr/) ([`zephyr/CMakeLists.txt`](zephyr/CMakeLists.txt) and [`zephyr/prj.conf`](zephyr/prj.conf)). The Zephyr `qemu_cortex_m3` board emulates a TI Stellaris LM3S6965 (`ti_lm3s6965`, `-cpu cortex-m3 -machine lm3s6965evb`) with **256 KB Flash** at `0x00000000` and **64 KB SRAM** at `0x20000000`.

```bash
# 1. Generate 32-bit C with an 8 KB per-stack reserve (5 stacks = 40 KB .bss arena)
./build/goose --m32 --stack-reserve 8K samples/01_tour.goose -o /tmp/01_tour_cm3.c

# 2. Build the Zephyr RTOS firmware image for qemu_cortex_m3
west build -b qemu_cortex_m3 -d build/zephyr zephyr --pristine -- \
  -DGOOSE_APP_SRC=/tmp/01_tour_cm3.c

# 3. Inspect Flash (.text) and SRAM (.data + .bss) footprint
arm-none-eabi-size build/zephyr/zephyr/zephyr.elf

# 4. Boot Zephyr RTOS and execute the Goose program in QEMU Cortex-M3
west build -d build/zephyr -t run
```

---

### `--m32` Test Results Across Samples and Benchmarks

#### Samples (`samples/*.goose`)

All non-GUI, single-threaded samples compile and run unmodified in `--m32` mode. Samples whose peak data-stack usage fits within the unmodified **64 KB SRAM** of the Zephyr RTOS `qemu_cortex_m3` target (after accounting for the ~10 KB Zephyr kernel, UART console driver, Newlib C runtime, and thread/interrupt stacks) were verified on both Zephyr `qemu_cortex_m3` and Wasm32; samples requiring > 64 KB of data-stack memory were verified on Wasm32 (`wasmtime`):

| Sample | Data Stack Regions | Zephyr `qemu_cortex_m3` (64 KB SRAM) | Zephyr ELF Footprint (`text` / `data` / `bss`) | Zephyr Flash / RAM Used | Wasm32 (`wasmtime`) | Notes |
|---|---:|---|---|---|---|---|
| `01_tour.goose` | 5 | **PASS** (`--stack-reserve 8K`) | `51,576 B` / `2,594 B` / `48,618 B` | `54,176 B` (20.7%) / `50,880 B` (77.6%) | **PASS** (`64K`) | Language tour |
| `02_memory.goose` | 10 | Needs > 64 KB SRAM | — | — | **PASS** (`16M`) | Pushes 100,000 `Item`s (~800 KB) |
| `03_strings.goose` | 16 | **PASS** (`--stack-reserve 3K`) | `86,736 B` / `2,594 B` / `57,134 B` | `89,336 B` (34.1%) / `59,392 B` (90.6%) | **PASS** (`64K`) | String builders, splits, formatting |
| `04_errors.goose` | 4 | **PASS** (`--stack-reserve 10K`) | `59,440 B` / `2,594 B` / `48,630 B` | `62,040 B` (23.7%) / `50,888 B` (77.7%) | **PASS** (`64K`) | Cross-frame `return ... from` |
| `05_shapes.goose` | 6 | **PASS** (`--stack-reserve 8K`) | `62,692 B` / `2,594 B` / `56,894 B` | `65,292 B` (24.9%) / `59,152 B` (90.3%) | **PASS** (`64K`) | Fixed & variable-mode ADTs (`Shape..`) |
| `06_functions.goose` | 7 | **PASS** (`--stack-reserve 7K`) | `60,812 B` / `2,594 B` / `51,898 B` | `63,412 B` (24.2%) / `54,160 B` (82.6%) | **PASS** (`64K`) | Generics and function blocks |
| `07_sieve.goose` | 7 | Needs > 64 KB SRAM | — | — | **PASS** (`16M`) | 10,000,001-byte prime sieve (~10 MB) |
| `08_bignum.goose` | 8 | **PASS** (`--stack-reserve 6K`) | `57,056 B` / `2,594 B` / `56,918 B` | `59,656 B` (22.8%) / `59,176 B` (90.3%) | **PASS** (`64K`) | Arbitrary-precision arithmetic |
| `09_sorting.goose` | 11 | Needs > 64 KB SRAM | — | — | **PASS** (`16M`) | Sorts large arrays |
| `10_sudoku.goose` | 5 | **PASS** (`--stack-reserve 8K`) | `61,260 B` / `2,594 B` / `48,650 B` | `63,864 B` (24.4%) / `50,912 B` (77.7%) | **PASS** (`64K`) | Backtracking solver + `os.time()` |
| `11_maze.goose` | 4 | **PASS** (`--stack-reserve 10K`) | `58,692 B` / `2,594 B` / `48,638 B` | `61,292 B` (23.4%) / `50,896 B` (77.7%) | **PASS** (`64K`) | Maze generation and BFS pathfinding |
| `12_huffman.goose` | 8 | **PASS** (`--stack-reserve 6K`) | `59,244 B` / `2,594 B` / `51,798 B` | `61,844 B` (23.6%) / `54,056 B` (82.5%) | **PASS** (`64K`) | Huffman tree encoding/decoding |
| `13_linked_list.goose` | 4 | **PASS** (`--stack-reserve 10K`) | `56,132 B` / `2,594 B` / `48,646 B` | `58,732 B` (22.4%) / `50,904 B` (77.7%) | **PASS** (`64K`) | `slot_pool` doubly linked list |
| `14_bst.goose` | 5 | **PASS** (`--stack-reserve 8K`) | `57,000 B` / `2,594 B` / `48,634 B` | `59,604 B` (22.7%) / `50,896 B` (77.7%) | **PASS** (`64K`) | Relative-reference (`Node&<u32>?`) BST |
| `15_word_freq.goose` | 7 | Needs > 64 KB SRAM | — | — | **PASS** (`16M`) | Reads & indexes `data/alice.txt` (167 KB) |
| `16_records.goose` | 7 | **PASS** (`--stack-reserve 7K`) | `77,756 B` / `2,594 B` / `51,906 B` | `80,360 B` (30.7%) / `54,168 B` (82.7%) | **PASS** (`64K`) | Variable-size nested records |
| `17_calc.goose` | 8 | — | — | — | **PASS** (`64K`) | Recursive-descent calculator (`< data/calc.stdin`) |
| `18_json.goose` | 11 | Needs > 64 KB SRAM | — | — | **PASS** (`16M`) | Parses `data/sample.json` |
| `19_vm.goose` | 4 | **PASS** (`--stack-reserve 10K`) | `57,652 B` / `2,594 B` / `48,606 B` | `60,256 B` (23.0%) / `50,864 B` (77.6%) | **PASS** (`64K`) | Bytecode VM |
| `20_life.goose` | 6 | **PASS** (`--stack-reserve 8K`) | `56,392 B` / `2,594 B` / `56,854 B` | `58,996 B` (22.5%) / `59,112 B` (90.2%) | **PASS** (`64K`) | Conway's Game of Life |
| `21_raytrace.goose` | 7 | Needs > 64 KB SRAM | — | — | **PASS** (`16M`) | Raytracer rendering scene buffer |
| `22_image_filters.goose` | 8 | Needs > 64 KB SRAM | — | — | **PASS** (`16M`) | Image convolution filters |
| `25_serialize.goose` | 26 | Needs > 64 KB SRAM | — | — | **PASS** (`16M`) | `to_bytes` / `from_bytes` verification on `alice.txt` |
| `26_file_tree.goose` | 6 | **PASS** (`--stack-reserve 8K`) | `64,332 B` / `2,594 B` / `56,934 B` | `66,932 B` (25.5%) / `59,192 B` (90.3%) | **PASS** (`64K`) | Two-pool directory tree |

*(GUI samples `27_gfx_cube`–`31_mini_doom` require SDL3 GPU windows; multithreaded samples `23_mandelbrot_threads` and `32_pool_worker_snapshots` require a `pthread`-enabled target.)*

#### Benchmarks (`bench/goose/*.goose`) on 32-Bit Wasm (`wasmtime`)

All 30 standalone benchmark programs in `bench/goose/` compile and pass unmodified in `--m32` mode on `wasm32-unknown-wasi` + `wasmtime`, producing identical output checksums to the 64-bit host while benefiting from 4-byte references and 8-byte slices/headers:

| Benchmark | Regions | `--stack-reserve` | Wasm32 (`wasmtime`) |
|---|---:|---:|---|
| `bintrees.goose` | 4 | `128M` | **PASS** |
| `blur.goose`, `blur_assert.goose`, `blur_rows.goose` | 5 | `128M` | **PASS** |
| `calc.goose`, `calc_noerr.goose`, `calc_noparen.goose`, `calc_slices.goose` | 5 | `128M` | **PASS** |
| `graph.goose`, `graph_csr.goose` | 4–6 | `128M` | **PASS** |
| `interp.goose` | 5 | `128M` | **PASS** |
| `lru.goose`, `lru_indices.goose`, `lru_nonopt.goose` | 5 | `128M` | **PASS** |
| `particles.goose`, `particles_scalar.goose` | 3 | `128M` | **PASS** |
| `push.goose`, `push_ref.goose`, `push_root.goose` | 4–5 | `128M` | **PASS** |
| `records_var.goose` | 5 | `128M` | **PASS** |
| `respond.goose`, `respond_stream.goose` | 6–7 | `128M` | **PASS** |
| `scene.goose` | 5 | `128M` | **PASS** |
| `sexp.goose` | 5 | `256M` | **PASS** (15.2M variable-size AST nodes + 95.8 MB source text) |
| `slot_pool_bench.goose` | 5 | `128M` | **PASS** |
| `strlist.goose` | 4 | `128M` | **PASS** |
| `sum.goose` | 3 | `128M` | **PASS** |
| `tree.goose` | 4 | `128M` | **PASS** |
| `words.goose`, `words_dictionary.goose` | 6 | `128M` | **PASS** |

## Why Goose

* **Faster than C++ and than safe Rust, while memory safe.** Over sixteen
  benchmarks Goose runs at 3.3x the speed of idiomatic C++, 1.16x hand-optimized
  C++ and 1.12x the best safe Rust, and uses 1.9x, 1.3x and 1.2x less memory
  ([results](bench/results.md)). These gains
  come from Goose's data layouts and memory model.
* **No allocator, no GC, no reference counting, no destructors.** Memory is a
  handful of data stacks that the compiler assigns statically. Freeing a
  structure with a million elements takes a single store, no matter how
  deeply it is nested.
* **Growth preserves references.** A reference into a growing array stays valid for as
  long as the array does. In the corresponding benchmarks, Goose keeps typed
  references where C++ must call `reserve` and safe Rust uses `u32` indices.
* **Memory safe with zero annotations.** There is no lifetime syntax, there
  are no aliasing or exclusivity rules, and there is no `unsafe`. The compiler
  infers what every reference is rooted at and rejects references that could
  outlive their owners.
* **Nested data stays inline.** A string, an array of strings, a record with
  variable-size fields and an array of those records are each stored as one
  contiguous block that contains no pointers. A example record that takes 160 bytes
  and one allocation in C++ takes 29 bytes and no allocations in Goose.
* **Variable sized Enums.** Variable-mode ADTs store each value at
  the size of its own variant rather than that of the largest variant. On the
  benchmark that exercises this, they use 4x less memory than a Rust `enum`
  and run at 2x its speed.
* **Links narrower than pointers.** A relative reference stores a typed, checked
  link as a 1, 2 or 4-byte offset. Structures built from them are position
  independent, so a data structure can be written to a file as is. Saving
  it is a single write, and loading it is a single read followed by a
  verification pass that rejects malformed or hostile data.
* **Everything is built in place, guaranteed.** A value is constructed at its
  final destination through any depth of calls. `items.push(parse(line))`
  writes the parsed record directly into the array, and returning a growable
  array by value does not copy it.
* **Returns across multiple calls.** `return err from load` returns from a
  function any number of frames up the call stack. It is checked statically,
  and needs no unwinder, no `Result` type and no `?` on every call.
* **Threads with no shared memory.** A worker is compiled as a separate program
  with its own memory, and flat values are passed between threads through
  typed queues with a `memcpy`. Data races, locks, atomics and memory
  orderings do not exist in the language.
* **Generics and higher-order functions with no overhead.** An untyped
  parameter is generic. Function values are compile-time entities, so
  `xs.filter() { it > 0 }` compiles to what the handwrittend loop would looks like and builds its
  result directly in its destination.
* **Compiles to C and can call C.** Goose compiles to one C file, so it runs wherever
  a C compiler does and calls C directly through `extern fn`. The bundled
  TinyCC backend compiles and runs a program in-process, with no build step.

The [tutorial](docs/tutorial.md) walks through all of this by example, the
[reference](docs/reference.md) covers the whole language topic by topic,
the [specification](docs/goose_spec.md) has the exact rules, and the
[benchmarks](bench/results.md) have the numbers, including the cases where
Goose loses.

## Features

This section focuses on what distinguishes Goose. The [tutorial](docs/tutorial.md)
explains these features in more detail, and the [samples](samples/README.md)
show them in thirty-one complete programs.

### One memory model: data stacks freed at scope exit

A program has the native call stack, static data and N data stacks, where the
compiler works out N. A data stack is a large address-space reservation with a
bump pointer, and there is no other memory. At most one resizable value is
live per stack and it is always on top, so growth never moves anything and
never checks a capacity. All of this is proved at compile time; the runtime
tracks only the bump pointers.

```goose
for round in 3 {
    var scratch: u8[>..] = [];                        // grow-only: growth is a pointer bump
    for i in 100000 { scratch.push((i % 256) as! u8); }
    print("round ", round, ": ", scratch.len, " bytes");
}                                                     // the free: one store to the stack top
```

### Memory never moves, so references survive growth

```goose
struct Item { id: i32, weight: f32 }

var items: Item[>..] = [];
let first .= items.push(Item { 1, 0.5 });             // a reference to element 0
for i: i32 in 2..1000001 { items.push(Item { i, 0.0 }); }
first.weight = 99.5;                                  // still valid, a million pushes later
```

`push` returns a reference to the new element, so a program can link elements
while building the array. A `vector<T>` or `Vec<T>` may reallocate and cannot
provide this guarantee. Stable references account for several benchmark gains.

### Safe references, no annotations

Every reference and slice has a static *root*: the variable that bounds its
target's lifetime. A reference must not outlive the variable that owns its
target or access that target through an incompatible type. The compiler infers
roots and specializes functions for them, so no annotations are needed. There
are no aliasing or exclusivity rules. When a lifetime check fails, the error
names the reference's root and its destination:

```goose
fn longest(a: u8[:], b: u8[:]) -> u8[:] { if a.len >= b.len { a } else { b } }

var outer: u8[>..] = [];
var w = outer[..];
{
    var inner: u8[>..] = [];
    format(inner, "inner text");
    w = longest(outer, inner);     // error: storing a reference rooted at inner,
}                                  //        which does not outlive the destination (§9.2)
```

A slice `T[:]` lets a function accept a range from any array kind without
copying its elements. For example, `split` returns slices into its input, and
a dictionary keyed by `u8[:]` stores views of strings rather than copies.

### Nested data stays inline

There is no single array type. Instead there is a family of array types that
differ only in how their size can change. Every member is laid out as
`[metadata][elements...]`, inline and packed, never as a pointer to a separate
block of elements:

| Type | What it is |
|---|---|
| `T[k]` | fixed size, known at compile time |
| `T[]`, `T[varint]` | sized at construction, frozen after; a fixed or variable-width length |
| `T[..k]`, `T[..]` | capacity inline; grows and shrinks within it |
| `T[>..]` | **grow-only**: the most commonly used kind; references into it stay valid |
| `T[>..<]` | **grow-shrink**: stacks, queues, heaps |

Strings are `u8` arrays: `u8[>..]` is a builder, `u8[]` a finished string
stored inline in whatever holds it, `u8[..16]` a small string inside a struct,
`u8[:]` a view. A struct may contain variable-size parts, which are stored
inline in declaration order, so a record is a sequence of bytes with no
indirection in it:

```goose
struct Item  { sku: u8[varint], qty: varint, cents: varint }
struct Order { id: varint, customer: u8[varint], items: Item[varint] }

var book: Order[>..] = [];
book.push(Order { id: 1001, customer: "alice",
                  items: [Item { sku: "SKU-441", qty: 2, cents: 1999 },
                          Item { sku: "SKU-7", qty: 1, cents: 500 }] });
```

That order is **29 bytes and 0 allocations**. As C++ `std::string` +
`std::vector<Item>` it is 160 bytes and 1 allocation, and as Rust `String` +
`Vec<Item>` it is 153 bytes and 4 allocations. An entire order book is a single
array, so reading it streams through the cache. The trade-off is that an array
of variable-size elements is sequential: you can iterate it but not index it
(unless you keep a second array of references into it as an index).

### Enums in two sizes

Algebraic data types are the only form of dynamic polymorphism; there is no
inheritance and there are no vtables. Every ADT can be stored in two ways,
chosen at the point of use. Fixed mode (`Shape`) stores a tag plus room for
the largest payload, so values can be indexed and overwritten. Variable mode
(`Shape..`) gives each value exactly the size of its variant. An array of
such values becomes sequential and a value never changes variant, but in
exchange you may take references *into* a payload:

```goose
enum Shape { Circle { r: f64 }, Rect { w: f64, h: f64 }, Dot }

var packed: Shape..[>..] = [];                 // 9, 17 and 1 bytes, not 17 each
packed.push(Shape.Circle { r: 1.0 });
packed.push(Shape.Rect { w: 2.0, h: 3.0 });
packed.push(Shape.Dot);
for s in packed {
    match s { Rect &r => { r.w += 1.0; }, _ => {} }   // edits the payload in place, inside the array
}
```

*Case functions* provide another way to dispatch on a variant: define one
overload per variant. Calls dispatch on the tag through a jump table, and the
compiler checks that the overloads cover every variant:

```goose
fn area(s: Shape.Circle) -> f64 { 3.14159 * s.r * s.r }
fn area(s: Shape.Rect) -> f64   { s.w * s.h }
fn area(s: Shape.Dot) -> f64    { 0.0 }

for s in packed { print(s, " area ", area(s)); }
```

### Links narrower than pointers

A `T&` is a machine address: it takes eight bytes and never dangles. A
*relative reference* is the same link stored as a narrow offset. A `T&<u32>`
is measured from the field itself to a target in the same array, so the
structure is position independent. A `T&<u32 in pool>` is measured from the
base of a named pool, so other arrays can link into the pool. `T&<u32>?` uses
offset 0 as null. Here is a binary search tree in one grow-only array, with
12-byte nodes:

```goose
struct Node { key: i32, left: Node&<u32>?, right: Node&<u32>? }   // 12 bytes, links included

fn insert(pool: Node[>..]&, key: i32) {
    if pool.len == 0 { pool.push(Node { key: key }); return; }
    var cur .= pool[0];                             // .= binds a reference; = would copy the node
    loop {
        if key == cur.key { return; }
        let next = if key < cur.key { cur.left } else { cur.right };
        if next { cur .= next; continue; }          // narrowed by `if`: next is a Node&
        if key < cur.key { cur.left .= pool.push(Node { key: key }); }
        else             { cur.right .= pool.push(Node { key: key }); }
        return;
    }
}
```

References are transparent (there is no `*` or `->`). `.=` binds or retargets
a reference, and `Node?` is a nullable reference that `if`, `guard` and
`assert` narrow. The insert is one push and one store, and the push cannot
invalidate `cur`. When lifetimes are not nested, the standard library's
[`slot_pool<T>`](docs/stdlib.md#slot_pool) pairs a grow-only `items` array
with a freelist. `alloc_ref` reuses a free slot or
pushes a new one, and `free` returns a slot to the pool. Since a slot's memory
is never actually freed, a stale slot reads a *different value of the correct
type* rather than corrupting memory.

[`slice_pool<T>`](docs/stdlib.md#slice_pool) provides contiguous runs with
allocation, free and resize operations. The language features `reusable` and
`reusable[]` are **deprecated** in favor of these pool types. They remain
available for existing code and the [remaining migration gaps](docs/design/root_structs.md#separate-follow-up-reusable-pools-in-the-stdlib);
removal is a later step.

### Saving and loading data as bytes

```goose
var image: u8[>..] = tree.to_bytes();           // a framed byte image of the whole array
var loaded, ok = from_bytes<Node[>..]>(image);  // verified before it becomes a value
image[image.len - 3] = 200;                     // tamper with a link ...
var bad, bok = from_bytes<Node[>..]>(image);    // ... and it is false and an empty array
```

The tree's links are self-relative, so the tree means the same thing wherever
it is stored. Saving it writes its bytes and loading it reads them back, with
no serializer, no schema and no pointer fixups. `from_bytes` checks the
framing, every tag, every length and every link before the bytes become a
value, so a corrupt or hostile file makes it return `false`, never a wild
reference.

### In-place construction

The copy-free construction guarantee
([spec §4.3](docs/goose_spec.md#43-the-in-place-construction-guarantee)) says a
constructed value is always built at its final location, which is passed down
through calls. A function returning a grow-only array by value writes its
elements directly into the caller's variable, or into a field of the record
being built inside another array, so out-parameters are rarely needed.

```goose
words.push(str("word", i));                    // formatted straight into the new element
let evens = xs.filter() { it % 2 == 0 };       // built straight into `evens`: no temporary
book.push(Order { id: 1001, customer: "alice", items: parse_items("SKU-441:2:1999;SKU-7:1:500") });
```

### Returning errors across calls

```goose
struct User { name: u8[..16], age: i32 }

fn load(text: u8[:]) -> User[>..], u8[] {
    var users: User[>..] = [];
    each_split(text, '\n') { users.push(parse_user(it)); };
    return users, "";
}

fn parse_user(line: u8[:]) -> User {              // returns a User: no Result, no error parameter
    let comma = find(line, ",");
    guard comma >= 0 else { return [], str("expected name,age: ", line) from load; }
    let age, ok = parse_int(line[comma + 1..]);
    guard ok else { return [], str("bad age: ", line) from load; }
    let name = trim(line[..comma]);
    return User { name: name, age: age as i32 };
}
```

`return E from f` returns `E` as the result of the innermost active call of
`f`, however many frames up that call is. Every function in between keeps its
plain signature. The construct is checked statically: every call of
`parse_user` must lie inside a `load` call, so nothing can go uncaught at
runtime. It is implemented as a hidden discriminant checked in each frame
rather than as an unwinder, and the message is built directly where `load`'s
caller wants it. The parsers in the samples use it for every syntax error.

### Generics and function blocks

```goose
fn twice(x) { x + x }                              // an untyped parameter is a generic one

fn each_pair<T, F>(xs: T[:]) {                     // F is a function value: a compile-time entity
    for i in 0..xs.len - 1 { F(xs[i], xs[i + 1]); }
}

fn first_gap(xs: i64[:]) -> i64 {
    each_pair(xs) { a, b => if b - a > 1 { return a; } };   // returns from first_gap, not each_pair
    return -1;
}

let evens = xs.filter() { it % 2 == 0 };
let total = fold(xs, 0) { acc, x => acc + x };
sort(xs) { a, b => a > b };
```

Everything is monomorphized. Type arguments are inferred from argument types;
type parameters that do not appear in those types can be supplied explicitly.
Function values are passed as generic parameters. Every call of a function
value is direct and can be inlined, function values cannot escape, and there
are no closure objects or function pointers, so a higher-order function
compiles to exactly the loop it looks like. A block may `return` from its
lexically enclosing function, and nested functions can use the enclosing
function's locals.

### Threads with no shared memory

```goose
thread_fn worker() {                                // a separate program with its own memory
    loop {
        let job = qget<Job>();                      // blocks on the typed queue for Job
        guard job.n >= 0 else { break; }            // -1 means stop
        qput(Result { n: job.n, digits: count_digits(job.n) });
    }
}

for i in n { ids.push(thread_spawn(worker)); }
```

There is no shared mutable memory. A `thread_fn` and everything it calls is
compiled as its own program with its own data stacks and its own copies of the
globals. Values are passed between threads through typed queues, one per
type, and must be *flat*, with no references at any depth. Passing them is
cheap because a flat Goose value is contiguous: a job or a result can carry
real data, such as pixels, and passing it is a single `memcpy`.

A failed check or `abort` in a worker started with `thread_spawn_contained`
ends only that worker. Its message goes to whoever waits for it, not to
stderr, so a supervisor can log it and start another:

```goose
var why: u8[>..] = [];
while !thread_wait(thread_spawn_contained(serve, attempt), why) {
    log(why);                                       // "assert failed (serve.goose:12)"
    why.clear();
    attempt++;
}
```

The first `thread_wait(id, msg)` for a worker takes the message; a caller
that doesn't ask for it loses it.

### Compiling to and calling C

```goose
extern fn hypot(x: f64, y: f64) -> f64;             // straight from libm
extern fn crc32_bytes(s: const u8[:]) -> u32;       // a slice crosses as { data, len }
extern fn stats_of(xs: i32[:], out: Stats&);        // a struct filled through a pointer
```

An `extern fn` binds a Goose signature to a C symbol. The `math` and `os`
modules use this interface. Only types with supported C representations may
cross the boundary; unsupported signatures are rejected at the declaration.
The compiler emits one C file for the whole program. The bundled TinyCC
backend can compile and run that C inside the Goose compiler's process.

### Where the speed comes from

Much of the speed comes from the memory model: no heap allocation or
per-object teardown, contiguous data, narrow links, and enums sized for the
variant they contain. Every index is bounds-checked, and the compiler removes
checks it can prove unnecessary. An `assert` on a slice length often lets the
compiler remove the remaining checks in a kernel, so that the kernel can be
vectorized. `--bce-lines` reports the checks that remain. `checkfree fn`
rejects a kernel if any bounds check remains in it; index types let graph
edges keep the validation done while the graph was built.
Benchmark measurements use whole-process wall-clock time, including teardown.

| Geometric mean over 16 benchmarks | vs idiomatic C++ | vs hand-optimized C++ | vs best safe Rust |
|---|---:|---:|---:|
| speed, MSVC backend | 3.51x | 1.24x | 1.13x |
| speed, clang backend | 3.54x | 1.26x | 1.19x |
| peak memory | 1.95x less | 1.32x less | 1.25x less |

The [summary](bench/summary.md) explains the gains and losses;
[results.md](bench/results.md) has every measurement, and
[design.md](bench/design.md) explains what the suite tests.

### Limitations

* You have to think about where data lives: who owns it, and how long its
  scope lasts. Usually the owner is the function that builds the data, which
  needs no extra work; when it is not, use a `slot_pool` or `slice_pool`.
* Recursive functions cannot keep growable data across a recursive call.
  Scratch data must end before the call, and data that outlives the call is
  passed in as a pool.
* Arrays of variable-size elements can be iterated but not indexed (unless
  you keep an index of references beside them). A fixed-mode enum cannot be
  pointed into, and a variable-mode one cannot be overwritten. You make this
  choice per container.
* There are no escaping closures, no function pointers and no dynamic
  dispatch beyond ADT tags, and only whole-program compilation is supported.
* A slot that was returned to a pool but is still referenced
  reads whatever its next owner put there. That is a logic bug, but never
  memory corruption.

[Tutorial §18](docs/tutorial.md#18-limitations) is the full list.

## Install a release

Prebuilt compilers for Windows (x64), Linux (x64) and macOS (Apple silicon
and Intel) are on the [releases page](https://github.com/aardappel/goose/releases),
with the graphics, audio, ui and physics packages compiled in. Unpack one
anywhere and put its directory on PATH: the compiler finds the standard
library, TinyCC's runtime and the packages beside itself, and keeps what it
builds under `~/.goose` (`GOOSE_HOME`). The archive also holds these docs, the
samples and the VS Code extension.

Running a program in-process (`goose file.goose`) needs nothing else on
Windows. On Linux it needs the C library's headers (`libc6-dev`, or
`glibc-devel`); on macOS, the SDK of the Xcode command line tools
(`xcode-select --install`). `goose build` also needs a C compiler: Visual
Studio 2022 or later, or clang, on Windows; gcc or clang elsewhere. The Linux
compiler needs glibc 2.35 or later (Ubuntu 22.04, Debian 12), and the macOS
compiler needs macOS 13.3 or later. macOS keeps a downloaded archive's files
from running until `xattr -dr com.apple.quarantine` is run on the unpacked
directory.

## Build and run

You need CMake 3.27 or later, a C++20 compiler (MSVC, clang or gcc) and Python
3 for the test and sample runners. TinyCC, which the in-process backend is
built from, is part of the tree. The graphics, audio, ui and physics
modules are packages in repositories of their own
([goose_gfx](https://github.com/aardappel/goose_gfx) and
[goose_physics](https://github.com/aardappel/goose_physics);
`docs/design/packages.md`). `-DGOOSE_BUILTIN_LIBS=all` fetches them with git
at configure time, at the pinned tags, and compiles them into the compiler,
so programs that use them run with nothing else installed. Without that
option the compiler is built on its own, and the first time a program
imports one of these packages, the package is built into `~/.goose`, which
needs git, cmake and a C compiler.

```bash
git clone https://github.com/aardappel/goose
cd goose
cmake -B build -DCMAKE_BUILD_TYPE=Release -DGOOSE_BUILTIN_LIBS=all
cmake --build build --config Release
```

`GOOSE_BUILTIN_LIBS` can also name some of the packages (`physics`, or
`gfx`, which includes `audio` and `ui` too). On
Linux, SDL needs the X11 or Wayland development packages
([`third_party/SDL/docs/README-linux.md`](https://github.com/libsdl-org/SDL/blob/main/docs/README-linux.md));
without them the build leaves the `gfx` module out and says what to install.

The runner scripts prefer `build/Release`, then `build/RelWithDebInfo`, then
`build`, then `build/Debug`. Pass `--exe` to select a specific compiler; use an
explicit Debug executable when testing with compiler assertions enabled.
The compiler is `build/goose` (`build/Release/goose.exe` with the Visual Studio
generator), and it finds the standard library in the source tree it was built
from. On Windows 10 version 1903 and later, its embedded manifest selects a
UTF-8 process code page so compiler paths do not depend on the system locale.
Earlier Windows versions retain their legacy code page. Generated native
executables retain their own process settings.

Run a program directly from source, in-process, with no C compiler and no
build step (`goose run` is the explicit form of the same command):

```bash
build/goose samples/01_tour.goose
```

Or build an executable, which is written beside the source. The compiler
finds a C compiler on the machine (`goose toolchain` shows which one) and
links the generated C with the runtime and any native modules the program
uses, including graphics. Programs that use threads must be built this way,
since TinyCC cannot place thread-local storage.

```bash
build/goose build samples/01_tour.goose && samples/01_tour
build/goose build samples/27_gfx_cube.goose && samples/27_gfx_cube
```

`goose check` only typechecks. `--debug` turns on the overflow, range and
tag checks, each of which names its source line when it fails, and
`--bce-lines` reports the bounds checks that remain on each line. Every
resizable value lives on a data stack of 2 GB of reserved address space,
committed as it is used, and `--stack-reserve 16G` changes that size.
[The command line](docs/command_line.md) describes every command and
option, including how to write the C for a build system or a C host of your
own (`goose -o tour.c tour.goose`).

A program with C of its own lists that C in a sidecar file beside the
source, `foo.build` beside `foo.goose`: `c_sources = ["impl.c"];`. Both
forms build that C, so an `extern fn` can call it either way. A program uses
a library by writing `import name from "source";` in its root file. The
source is a directory (`"./lib"`), or a git repository at a tag or commit
(`"owner/repo@v1"` on GitHub, `"host/owner/repo@ref"`), which is fetched
into `~/.goose` and built there once per C compiler
([packages](docs/packages.md)).

A program using `audio` plays procedural mono/stereo PCM through SDL3 without
opening a graphics window. See the [audio reference](docs/stdlib.md#audio).

The test suite and the samples run on Windows, macOS and Linux:

```bash
python test/run_tests.py
python samples/run_samples.py
```

For editing, the [VS Code extension](vscode/README.md) gives syntax
highlighting, the compiler's language server (`goose lsp`: errors as you
type, hover, go to definition, completion, rename, symbols) and one-key runs
through the JIT:

```bash
code --install-extension vscode/goose-language.vsix
```

Other editors can run `goose lsp` themselves; [editors](docs/editors.md)
has the configuration for Neovim, Helix and Emacs.

## Documentation

* [Documentation index](docs/index.md): the map of everything below, and
  the root of the documentation site built from these files
  ([how](docs/site/README.md)).
* [Tutorial](docs/tutorial.md): the friendly introduction, by example. Read
  this first.
* [Language reference](docs/reference.md): every feature, topic by topic,
  with a compiling example for each. It covers the whole language and is
  written to be read.
* [Language specification](docs/goose_spec.md): the exact rules, when you want
  to know why something did not compile.
* [Samples](samples/README.md): thirty-one complete programs in reading
  order, from a tour of the language to a JSON parser, a threaded Mandelbrot, a
  file tree built from two pools, a spinning cube on the GPU, thousands of
  boxes raining into a heap and a to-do list in windows of widgets.
* [Standard library](docs/stdlib.md): fourteen modules, all written in readable
  Goose under `stdlib/`. They include `gfx` for graphics on SDL3's GPU API ([how
  it is built](https://github.com/aardappel/goose_gfx/blob/main/docs/gfx.md))
  and `physics` for rigid body physics on Box3D ([how it is
  built](https://github.com/aardappel/goose_physics/blob/main/docs/physics.md)).
  The `ui` module provides fonts, [text rendering and game
  HUDs](docs/stdlib.md#text-rendering-and-game-huds). It also provides windows
  and widgets on Nuklear, drawn through gfx ([how it is
  built](https://github.com/aardappel/goose_gfx/blob/main/docs/ui.md)).
* [The command line](docs/command_line.md): every command and option of
  `goose`, from running and building to writing the C yourself, the
  formatter and the language server.
* [Building programs and using packages](docs/packages.md): libraries from
  git repositories or directories, a program's own C, and how to make a
  package of your own.
* [Benchmarks](bench/summary.md): the numbers, with the
  [full results](bench/results.md) and the [design](bench/design.md) behind them.
* [Implementation notes](docs/implementation.md): how the compiler works, pass
  by pass, and [how it is tested](docs/testing.md).

## Status

Goose is new. What exists today is a whole-program compiler of about thirty
thousand lines of C++, the specification, the standard library, the samples,
and a test suite that CI runs on Windows, macOS and Linux with an extra
sanitizer job. These are deliberately out of scope for now: moves for
resizable values, more than one resizable per struct, labeled `break`,
namespace privacy, and more OS/library access. Open items are tracked in the
[specification's Appendix B](docs/goose_spec.md#appendix-b-todo--open-items).

## License

Goose is licensed under the [Apache License, Version 2.0](LICENSE).

## Authors

**Wouter van Oortmerssen**: Language Design, Compiler Design, Coding standards.
[home](https://strlen.com/) page, [twitter](https://x.com/wvo).

**Claude Fable**: Compiler implementation, Benchmarking, Sample & Doc writing.

## History & Use of AI

This repo was produced almost entirely with AI, from a human language design.
I had designed Goose several years ago, and had started to implement it,
but running a game startup (which is built on another programming language of mine,
[Lobster](https://strlen.com/lobster/)) there was no time to finish it.
Which was sad, because I knew Goose could do things other languages can't,
and it should exist.

I had not expected AI to help with this until Fable came out. I thought it
might be capable enough to do a good job.
I made it essentially clone the style and structure of my other recent compiler
(Lobster), which is why if you look at the code, it looks rather similar to that.
My initial design had left lots of things unspecified, and lots of back and forth
with Fable made me decide on all of those, and it is now a better language for it.

It is also an experiment in using AI to implement a new language, rather than
reproduce an existing one.

You may wonder why I had it work in C++, if clearly I could have used any
language, like Rust, or my own Lobster, or.. Goose itself (that may still happen).
With Fable, it was my observation that it is almost equally capable in any
language, and the strong guardrails of Rust or other languages are not as pressing
as they once were.
Since the compiler emits C, you likely already need to have a C/C++ compiler around,
so sticking inside one ecosystem would seem to simplify deployment and adoption.
I've also integrated libtcc and who knows what other C libraries in the future.
And, like I said, to have the compiler code mimic my existing Lobster compiler seemed
fun, at least I can read it like it is my own.
