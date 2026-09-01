# mu_sq / ESP-seq TX indexing — test results

Dated record of the validation work on the mu_sq (Multi-User SQ) feature: DOCA Flow
steering IPsec-ESP traffic by its own sequence number, copying that number into a
hardware metadata register, and the mlx5 PMD placing each packet's WQE in the shared
send queue by that same number instead of a per-core local counter. Covers correctness
validation (packet ordering, file integrity, queue-count sweep) and a CPU-load scaling
comparison against a single-core baseline.

Hardware: ConnectX-9, single 400GbE port (`0000:17:00.1`), NUMA node 0. Traffic
generator: TRex on a back-to-back host (`danger50`), reachable at
`/homes/ofekdg/trex_scripts/`.

---

## 2026-09-01 — Implementation, crash found and fixed, functional validation

### What was built

- **DOCA pipeline** (`doca_pipelines_sdk/src/doca_pipelines/doca_pipelines_esp_rr.cpp`,
  vendored copy under this repo): `LB_ESP_RR_ROOT` matches IPv4+ESP, copies the real
  ESP Sequence Number (`tun.esp_sn`) into `meta.pkt_meta` via a new `ActionCopyEspSnToMeta`
  (`DOCA_FLOW_ACTION_COPY`, field_string `"tunnel.esp.sn"`), forwards into
  `LB_ESP_RR_STEER`, which masks-matches on `meta.pkt_meta` (new `MatchPktMetaMasked`)
  and RSS-forwards one queue per masked value. Selected via `DOCA_PIPELINES_MODE=esp_rr`.
  Decap (stripping the outer ESP framing) is implemented but **not enabled** — see
  "Open items" below.
- **mlx5 PMD** (`drivers/net/mlx5/`): `rte_flow_dynf_metadata_register()` called
  driver-side (`mlx5_trigger.c`, gated on `mu_sq_log_grp_size != 0`), so the ESP SN
  reaches the mbuf via the existing generic RX/TX metadata-dynfield mechanism with no
  application changes. New `mlx5_tx_get_esp_sn_from_meta()` reads it (no payload
  access). `mlx5_tx_burst_single_send()`'s `single_no_inline` path computes
  `wqe_ci = (esp_sn * MLX5_MU_WQE_SIZE) & wqe_m` directly from the packet's own
  sequence number instead of a per-lane local counter, with a hard safety gate
  (never write to a slot outside this lane's own column) and a same-value fallback
  to the original fixed-stride addressing for gap/late/missing-metadata cases.
  eMPW (`MLX5_TXOFF_CONFIG_EMPW`) is excluded whenever `mu_sq_log_grp_size != 0`
  (`mlx5_tx.c`, `mlx5_select_tx_function()`) — see the crash below for why.

### Crash found and fixed (real hardware)

First attempt at the "unsafe to place" cases (lane mismatch / seq arrived "late")
called `rte_pktmbuf_free()` directly on those packets. This is a **double-free**:
`mlx5_tx_copy_elts()` (the caller, `mlx5_tx_burst_single()` in `mlx5_tx.c`)
bulk-copies every mbuf pointer in a burst into `txq->elts[]` and advances
`elts_head` by the full count *before* the per-packet loop runs — every packet is
already committed to a future completion-driven free, regardless of whether a real
WQE is ever built for it. Freeing one directly, on top of that, corrupted the
mempool and reproducibly segfaulted `dpdk-worker2` inside
`mlx5_tx_burst_single_send` (confirmed via `dmesg`: `segfault at 8`, SIGSEGV,
deterministic at ~100 packets/burst).

**Fix**: never skip WQE construction. Every packet always gets a WQE at *some*
address — the seq-derived one on the fast path, or the pre-existing fixed-stride
address as a fallback for lane-mismatch/late/gap/missing-metadata cases. The
"never touch another lane's column" safety property is satisfied by which address
is chosen, not by refusing to send. Verified fixed at 100 and 10,000-packet bursts,
repeated at full production load later in this document with no recurrence.

An eMPW-exclusion fix was also applied as a precaution (see above) — `single_no_inline`
and `mlx5_tx_burst_empw_simple()` share `txq->wqe_ci` state, and empw was never
updated for the seq-derived, non-monotonic addressing scheme.

### Task 1 (ESP-seq steering) confirmed working end to end

Cross-checked against independent work on the same feature in a separate
(standalone) repo, which had already confirmed `field_string = "tunnel.esp.sn"` is
correct with real traffic, and that enabling decap independently breaks RX delivery
entirely (zero packets reach any RX queue) — an exact match for what was seen here.
Retested in this repo with the real `ActionCopyEspSnToMeta` and decap left disabled:

| Metric | Result |
|---|---|
| `opackets` | 10,000 / 10,000 sent |
| `tx_q0` / `tx_q1` split | 5,000 / 5,000 (exact round-robin of a real incrementing ESP SN) |
| DOCA `esp_rr_steer_q0` / `q1` | 5,000 / 5,000 |
| DOCA `root_hit` | 10,000 |
| Loss | 0% |

### Functional validation — payload ordering and file integrity

Two round-trip tests via TRex, run at 2, 4, and 8 queues/cores
(`mu_sq_log_grp_size` = 1/2/3):

1. **Sequential payload ordering**: 10,000 ESP-framed packets, each carrying an
   independent 4-byte incrementing marker in the payload's last 4 bytes (decoupled
   from the ESP SN field used for steering, so this checks the actual returned
   bytes at the traffic generator, not internal counters). Captured via TRex's
   `start_capture`/`stop_capture` (requires `set_service_mode(enabled=True)` first —
   without it, capture silently records 0 packets despite RX stats showing traffic
   arrived).
2. **File integrity**: a 256 KB random binary file, chunked into 188 ESP-framed
   packets (chunk index = ESP SN, 2-byte length trailer for the last chunk),
   round-tripped through the DUT, reassembled by SN from the capture, and
   byte-compared (SHA-256 + `cmp`) to the original.

| Queues | Test 1: order violations | Test 1: missing/duplicate | Test 2: file integrity | DOCA steer split |
|---|---|---|---|---|
| 2 | 0 / 10,000 | 0 / 0 | Byte-identical | 5,094 / 5,094 |
| 4 | 0 / 10,000 | 0 / 0 | Byte-identical | 2,547 × 4 |
| 8 | 0 / 10,000 | 0 / 0 | Byte-identical | 1,274×4, 1,273×4 |

Note on Test 2: chunks were sent as concurrently self-started TRex streams (a
`push_pcap()`-based approach didn't surface in TRex's capture for reasons not
chased down), so arrival order was genuinely scrambled at the wire (e.g. 94/188
chunks out of SN order at 2 queues) — reconstruction is keyed by SN, not arrival
position, so the file still came back byte-perfect regardless. Test 1 (paced,
single-stream) is the real ordering proof; Test 2 additionally shows reconstruction
is robust even under a chaotic send pattern.

No crashes at any queue count. Clean SIGINT shutdown every run.

### Open items (not yet resolved)

- **Decap** (stripping the outer ESP framing before the app sees the packet) does
  not work — implemented (`doca_flow_actions.decap_type`/`decap_cfg`, `is_l2=false`)
  but silently breaks RX delivery entirely when enabled (zero packets reach any
  queue, no error at pipe-creation time). Confirmed independently in the standalone
  repo's parallel work too. Leads to try, most promising first: populate
  `decap_cfg.eth`/`l2_valid_headers` (left zero so far); or ESP may need the
  crypto-action framework (`doca_flow_crypto.h`) instead of the generic tunnel-decap
  struct used for VXLAN/GRE/GENEVE.
- **`mlx5_tx_burst_empw_simple()`** (the eMPW multi-packet-per-WQE batching path)
  was not given the seq-derived addressing fix — it batches several packets into
  one WQE/one doorbell, which doesn't map onto per-packet indexing without a
  batch-boundary redesign. Currently excluded entirely whenever mu_sq is active
  (see above), so this is a scope limitation, not a live bug.
- Decap's eth header placeholder (all-zero MACs) is a stub, irrelevant while decap
  stays disabled.

---

## 2026-09-02 — CPU-load scaling: single-core baseline vs. mu_sq multi-core

### Goal

With Task 1 validated, characterize how mu_sq's shared-queue, seq-indexed design
scales under artificial per-packet CPU load, against a single-core baseline with no
mu_sq/DOCA code at all.

### Method

- **Busy-wait**: a TSC-cycle spin (`rte_delay_us_block`-equivalent, never yields to
  the scheduler), injected once per received packet in a plain `for` loop in
  `lpm_main_loop()` (`examples/l3fwd/l3fwd_lpm.c`) — not inside the x86 SIMD
  4-packet batch path, so it scales linearly with packet count regardless of burst
  size. Configured via a new `L3FWD_BUSY_WAIT_NS` env var, read once at startup.
- **Utilization**: real (non-`top`-inflated) per-lcore busyness via DPDK's
  `rte_lcore_register_usage_cb()` / `/eal/lcore/usage` telemetry endpoint — the app
  reports which cycles were genuinely spent on work vs. idle polling, since a
  polling app always reads as 100% CPU to the OS regardless of real load. Queried
  via a small one-shot client against the `dpdk_telemetry.v2` UNIX socket.
  Before/after deltas over a **20-second** sustained burst (a 3-second first attempt
  diluted the ratio with ~3s of fixed SSH/TRex-connect overhead, flattening it to a
  uniform ~50% regardless of true load — the 20s window fixed this).
- **Baseline**: vanilla upstream `main` (no mu_sq, no DOCA integration at all),
  checked out into an isolated git worktree (`/homes/ofekdg/prom-dpdk-main-baseline`)
  so the feature branch's uncommitted work was never touched. Single queue, single
  core (lcore 0), plain UDP traffic to the DUT's configured LPM route (no ESP
  framing needed — nothing to steer).
- **mu_sq runs**: `DOCA_PIPELINES_MODE=esp_rr`, `esp_rr_routable.py` TRex profile
  (real incrementing ESP SN, routable inner ICMP payload), 2/4/8 queues/cores.
- Both sides offered a fixed 5,000,000 pps for 20s (100,000,000 packets) per run.

### Single-core baseline: busy-wait sweep

| Busy-wait | Achieved pps | Busyness ratio | Loss |
|---|---|---|---|
| 0 ns | ~5,000,000 (kept up fully) | 76.3% | 0% |
| 200 ns | ~2,382,150 | 83.3% | 52% |
| 500 ns | ~1,405,235 | 86.0% | 72% |
| 1,000 ns | ~840,842 | 85.8% | 83% |
| 2,000 ns | ~447,936 | 87.1% | 91% |
| 5,000 ns | ~191,323 | 87.4% | 96% |
| 10,000 ns | ~97,747 | 86.2% | 98% |
| 20,000 ns | ~49,501 | 87.7% | 99% |

Achieved throughput matches `1 / (busy_wait_ns + ~200ns)` almost exactly at every
point (the ~200ns is this build's baseline per-packet processing cost with zero
simulated delay). Busyness ratio plateaus at ~86–88% for every value ≥200ns — the
dilution ceiling given the fixed ~3s setup overhead over a 20s window — confirming
the core is genuinely, fully saturated starting at just 200ns of simulated work.
Only the 0ns case is actually below saturation.

### mu_sq multi-core: 2/4/8 cores × {2000, 5000, 10000} ns

Achieved throughput (received pps over the 20s window):

| Busy-wait | 1 core (baseline) | 2 cores | 4 cores | 8 cores |
|---|---|---|---|---|
| 2,000 ns | 447,936 | 746,937 | 1,483,359 | 2,969,232 |
| 5,000 ns | 191,323 | 351,935 | 702,355 | 1,402,852 |
| 10,000 ns | 97,747 | 187,156 | 374,147 | 748,358 |

Scaling efficiency (achieved ÷ ideal-linear = single-core pps × core count):

| Busy-wait | 2 cores | 4 cores | 8 cores |
|---|---|---|---|
| 2,000 ns | 83.4% | 82.8% | 82.9% |
| 5,000 ns | 92.0% | 91.8% | 91.7% |
| 10,000 ns | 95.7% | 95.7% | 95.7% |

**Key finding**: scaling efficiency is constant across 2/4/8 cores at every
busy-wait value (spread ≤0.6 percentage points at any fixed load) — it depends only
on per-packet cost, not on core count. That's the signature of a fixed, small
per-core overhead that does not grow as more cores join; no shared-resource
contention bottleneck appears as core count increases from 2 to 8. Efficiency also
*improves* as simulated per-packet work grows (83% → 92% → 96%), consistent with a
roughly constant ~400ns "tax" specific to this branch (ESP steering + COPY +
seq-indexed TX) that dilutes into insignificance as real per-packet work dominates.

Per-lcore telemetry on the 8-core runs confirmed even load distribution: all 8
cores within 0.5 percentage points of each other's busyness (82.2–82.7% at 2000ns,
86.6–86.7% at 10000ns) — no straggler core.

DOCA `root_hit` counts were occasionally 1–3% below the full 100M packets offered
at higher core counts (e.g. 96.86M at 4 cores/2000ns) — a small amount of loss
upstream of the CPU bottleneck itself. Doesn't change the scaling conclusion, but
worth isolating separately from the CPU-bound throughput ceiling measured here.

All 17 test runs (8 baseline + 9 mu_sq) completed with clean SIGINT shutdown, no
crash, no hang.

### Artifacts from this run

- Sweep driver scripts and raw logs: `/tmp/baseline_sweep_logs/`,
  `/tmp/musq_sweep_logs/` (scratch, not committed).
- Published results report (charts + data table):
  `https://claude.ai/code/artifact/af7cbef5-0ab0-4193-b2ad-e0c2ea9eb4ab`
- `main`-branch baseline worktree: `/homes/ofekdg/prom-dpdk-main-baseline` (kept for
  further comparisons; remove with `git worktree remove` when no longer needed).
