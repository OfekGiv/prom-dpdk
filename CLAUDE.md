# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

This is a personal fork of DPDK (based on the upstream `main` branch, see `remotes/upstream/main`), used for
mlx5 (ConnectX/BlueField) PMD driver development. The active work areas are:

- `drivers/net/mlx5/` — the mlx5 net PMD: rx/tx datapath (`mlx5_rx.c`, `mlx5_tx.c`, `mlx5_tx.h`, `mlx5_rxtx*`),
  device config/probe (`mlx5.c`, `mlx5.h`, `mlx5_devx.c`), and tracing (`mlx5_trace.c/h`).
- `doca_pipelines_sdk/` — a locally vendored C++ SDK (headers/libs under `include/` and `lib/`, sources under
  `src/`) that wraps NVIDIA DOCA flow/graph/pipeline APIs. It is built as a meson subproject
  (`doca_pipelines_sdk/meson.build`) producing `doca_pipelines_meta_rr_dep`, which `examples/l3fwd` links against
  (see `examples/l3fwd/meson.build`). This is the only example wired up to DOCA pipelines.
- `examples/l3fwd/` — the primary test application used to exercise driver changes.

Current focus (see recent commits and in-progress diffs): a "Multi-User SQ" (mu_sq) feature in the mlx5 PMD —
grouping multiple send queues, configured via the per-device devarg `mu_sq_log_grp_size` (see `mlx5.c` and the
`mu_sq_log_grp_size` field in `struct mlx5_dev_config` in `mlx5.h`) — plus DOCA pipeline/round-robin integration
and rx/tx tracing instrumentation.

Don't try to infer intent for `mu_sq_*` or DOCA-pipeline code purely from upstream DPDK conventions; it's
custom to this fork. When in doubt, check `readme` and recent commit messages (`git log --oneline`) for what is
actively being debugged.

## Build

There are multiple parallel build directories already configured (`build`, `build-test`, `build_debug_doca`,
`build-doca-w-debug`), each meson-configured with different options. Do not assume there is one canonical build
dir — check `<builddir>/meson-info/intro-buildoptions.json` if it matters, or ask which one is in use. When
setting up a fresh build directory that needs DOCA pipelines + l3fwd, this is the pattern used (see `readme`):

```sh
meson setup <builddir> --prefix=<installdir> --buildtype=debug -Ddefault_library=shared -Dexamples=l3fwd -Denable_trace_fp=true
ninja -C <builddir>
ninja -C <builddir> install
```

`-Denable_trace_fp=true` (sets `RTE_ENABLE_TRACE_FP`, see `meson_options.txt`) is required for the rx/tx
fast-path trace points (`pmd.net.mlx5.rx.lcore`, `pmd.net.mlx5.tx.lcore`, `mu_trace_tx_lcore`, etc. in
`mlx5_trace.h`) to actually compile in and fire — without it those `RTE_TRACE_POINT_FP` calls are compiled out.

Build only the mlx5 driver / l3fwd example incrementally:

```sh
ninja -C <builddir> drivers/net/mlx5/libtmp_rte_net_mlx5.a
ninja -C <builddir> examples/dpdk-l3fwd
```

`compile_commands.json` is generated per build dir; `.clangd` points `CompilationDatabase` at `build` — if you
work primarily out of a different build dir, regenerate or symlink accordingly.

## Running / debugging l3fwd

l3fwd needs hugepages and a real mlx5 NIC (PCI address varies per host — see `.vscode/launch.json` /
`.vscode/tasks.json` and `readme` for concrete examples, they use specific PCI BDFs and rule-DB paths tied to
this machine). General shape of an invocation:

```sh
echo 1024 | sudo tee /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages
echo 1024 | sudo tee /sys/devices/system/node/node1/hugepages/hugepages-2048kB/nr_hugepages

sudo env -i LD_LIBRARY_PATH=<installdir>/lib/x86_64-linux-gnu:<installdir>/lib/x86_64-linux-gnu/dpdk/pmds-26.0:doca_pipelines_sdk/lib/x86_64-linux-gnu:/opt/rdma-private/lib/x86_64-linux-gnu \
  gdb --args <builddir>/examples/dpdk-l3fwd -l 1-4 -n 4 -a <pci_bdf>,mu_sq_log_grp_size=<N> \
  --trace=pmd.net.mlx5.rx.lcore --trace=pmd.net.mlx5.tx.lcore --trace-dir=<tracedir> \
  -- -P -p 0x1 --config="(0,0,1),(0,1,2),..." --rule_ipv4=<path> --rule_ipv6=<path> --queues <N>
```

Key points:
- `mu_sq_log_grp_size=<N>` is a custom mlx5 devarg (log2 of the SQ group size for the mu_sq feature).
- `sudo env -i` is used deliberately to run with a clean environment plus an explicit `LD_LIBRARY_PATH` —
  needed because the DOCA/RDMA private libs live outside the default library search path.
- rte_trace points `pmd.net.mlx5.rx.lcore` / `pmd.net.mlx5.tx.lcore` (defined in `mlx5_trace.h`) are the main
  datapath tracing hooks used while debugging; traces are written under `--trace-dir` as CTF and can be decoded
  with `drivers/net/mlx5/tools/mlx5_trace.py` (requires the `bt2`/babeltrace2 Python bindings).

## Architecture notes specific to this fork

- **mlx5 tracing** (`mlx5_trace.h`): trace points are declared with `RTE_TRACE_POINT_FP`/`RTE_TRACE_POINT` and
  gated at call sites with `rte_trace_is_enabled()` before emitting, since these are hot-path rx/tx functions
  (see `mu_trace_tx_lcore` usage in `mlx5_tx.h`'s `mlx5_tx_burst_single_send`). When adding a field to a trace
  point signature, update both the `RTE_TRACE_POINT*` declaration in `mlx5_trace.h` and every call site.
- **DOCA pipelines SDK** (`doca_pipelines_sdk/`): treat `include/` as a vendored snapshot of DOCA headers (not
  meant to be hand-edited) and `src/doca_pipelines*.cpp` as the actual glue code owned by this project. It links
  against prebuilt `.so`s in `doca_pipelines_sdk/lib/x86_64-linux-gnu` (`doca_flow`, `doca_dpdk_bridge`,
  `doca_common`, `nvhws`) via `-Wl,-rpath`.
- Standard DPDK meson conventions apply everywhere else (driver registration via `drivers/net/mlx5/meson.build`,
  `dpdk_driver_classes`, etc.) — see upstream DPDK docs for anything not mu_sq/DOCA specific.

## Repo hygiene

- `install-test/`, `install-w-debug/`, and the various `build*/` directories are local build artifacts, not
  checked in — don't treat them as source of truth for anything, and don't add files there expecting them to
  persist/be reviewed.
- There are many local branches (`mu-sq-*`, `doca-*`, `prom-*`, backups) beyond `main` — when asked to compare
  or base work off "the last known good state," confirm which branch, don't assume `main`.
