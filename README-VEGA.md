# Hookshot for AMD Vega / Linux: an AI-driven fork with measured fixes over upstream

This branch (`vega-linux`) is a fork of [NotRexed/Hookshot](https://github.com/NotRexed/Hookshot) at upstream commit
`581afaf`, with fixes and opt-in switches for GPUs without mesh shaders (AMD Vega / GFX9 and RDNA1 integrated GPUs) on
Linux (RADV). Everything here was found, written and measured with AI agents (Claude Code) working under human direction:
a person chose the goals, played the builds, judged every picture and accepted or rejected each change. Each change is
its own commit with the reason and the measured effect in the message.

Unless a switch is set, the behaviour stays as upstream's, apart from the correctness fixes listed under
[Defaults that differ from upstream](#defaults-that-differ-from-upstream).

## What it fixes

- **Missing level geometry on GPUs without mesh shaders.** The level models (walls, doors, roofs, platforms, the pushable
  block in the Great Deku Tree, the platform at Lake Hylia) are task + mesh draws. On a GPU without mesh shaders they go
  through the compute fallback, which (1) copied the task payload lane by lane, so every mesh workgroup but one read a
  meshlet count of 0 and emitted nothing, and (2) ran indirect task draws with 0 slots. Collision was intact, so these
  were invisible walls. Fixed by `RX_VK_MESH_PAYLOAD_WG=1` (one payload per task workgroup, as on native mesh hardware)
  and the indirect slot fix. `RX_VK_MESH_LEAN=1` then makes the fallback cheap enough to play with: only the started
  meshlets run, with no per-draw payload clear.
- **The second-launch speckle bug of the shader cache.** The cold prepare stored mesh fallback programs as native mesh
  programs. The next launch built native-mesh pipelines for them (no input assembly), so every fallback draw came out as
  points: grass gone, Hyrule Castle and Castle Town a cloud of cyan speckles, trees as white blobs. The first launch looked
  fine, every later one was broken. The fix records them the way play makes them. Changing the file changes the shader
  stamp, so old caches are prepared again once.
- Smaller AMD fixes from before: the GPU timeout during big scene loads (uploads are now split into several submits), a
  fence that was signalled after the first part of a long submission, rendered targets overwritten by stale buffer data,
  BC textures with uneven tail levels that hung the GPU, and 32-lane warp emulation on GFX9 (it cannot run 32-wide
  subgroups).

## Recommended setup for Vega / RDNA1 integrated GPUs

`hookshot_env.txt` in the game folder (one per line, or as environment variables):

```ini
RX_VK_NO_MESH=0
RX_VK_MESH_LEAN=1
RX_VK_MESH_NARROW=1
RX_VK_OOT_GMEM=1
RX_VK_OOT_GM_BASE=1
RX_VK_NO_NAN_GUARD=1
RX_VK_BATCH_IDLE=1
RX_VK_ORDER_WAKE=1
RX_VK_SEG_FIRST=2
RX_VK_SIGNAL_FLUSH=1
RX_VK_BARRIER_NARROW=1
RX_VK_BARRIER_DEFER=1
RX_VK_DISPATCH_INDEP=1
RX_VK_PIECE_FLUSH=64
RX_VK_FRAME_WAIT=2
RX_VK_PRESENT_FPS=30
HOS_GAME_FPS=30
HOS_SPIN_US=0
HOS_SHORT_SLEEP_TIMER=1
RX_VK_PARALLEL=3
RX_VK_UPLOAD_SPLIT_MTEXELS=1
RX_VK_TEX_MAX_DIM=2048
RX_VK_TEX_MAX_MB=256
RX_VK_VRAM_MB=6000
RX_VK_NETWORK_KEEP=fd029a317a9a4b94,9dc98871eb7fc101,72ba090c70fe2614
```

In-game: handheld profile on, Render resolution 67%, no upscaling filter, no sharpening, no frame generation
(`handheld_profile=1 resolution=3 upscaling=0 sharpness=0 frame_generation=0` in `hookshot_options.ini`).

Notes:
- `RX_VK_NO_MESH=0` with `RX_VK_MESH_LEAN=1` is what brings the walls back. With `RX_VK_NO_MESH=1` the game runs ~30%
  faster but every task/mesh model is missing.
- The `RX_VK_OOT_*` and `RX_VK_MESH*` variables are part of the program cache fingerprint: changing them prepares the
  shader cache again (~2 min on this machine).
- `RX_VK_FRAME_WAIT=2` lets the CPU record one frame while the GPU draws the one before. `RX_VK_PRESENT_FPS=30` and
  `HOS_GAME_FPS=30` give an even 30 fps; set both to 0 for uncapped.
- `RX_VK_VRAM_MB` is sized for a machine with plenty of shared memory; lower it on a smaller one.
- `RX_VK_DISPATCH_INDEP=1` also handles one particle program by its program key, which this fork does not ship. Without
  `RX_VK_INDEP_PARTICLE_KEY=<key>` only the general rule applies, so the gain may be smaller than measured below.

## Results

Hardware: Ryzen 7 5850U (Zen 3, 8 cores), Radeon Vega 8 integrated GPU (GFX9), Linux, RADV from Mesa 26.0.8, 22 W
sustained package power. 67% render resolution, uncapped, unless noted.

### Method

- **Paired live A/B in one launch.** `RX_VK_LIVE_CFGS=<file>` makes the running game cycle through configurations of the
  runtime switches (A B C A B C ...), each held for 3 s after a 20-present settle, for 10 to 20 rounds, while the player
  stands still in one spot. Each round gives one paired difference per configuration against `base`. Thermal drift and
  scene changes hit every configuration alike.
- **Statistics.** Median of the per-round differences, a sign-flip permutation p and a Wilcoxon signed-rank p
  (`tools/vega-bench/livestat.py`). A result counts only when both are under 0.05.
- **Sanity row.** Every run has `base2`, a second copy of `base`. It has to come out near 0% and not significant, and
  it did (for example -0.6% p .76 / .07, +0.1% p .98 / .79, -0.4% not significant).
- Switches that change the translated shaders (`RX_VK_OOT_*`) cannot be flipped live. Those were measured as interleaved
  pairs of whole launches (A B A B A B) in the same spot.
- Pictures were checked by eye. Black-frame checks used a screenshot of every frame on a Hyrule Field route.

### Overlap and barrier switches (live A/B, Kokiri Forest exit, mesh off, 14 rounds, base 29.7 fps)

| configuration | fps vs base | p perm / Wilcoxon |
|---|---|---|
| base2 (sanity) | -0.6% | .76 / .068 |
| BATCH_IDLE + ORDER_WAKE + SEG_FIRST=2 + SIGNAL_FLUSH | +4.6% | .004 / .007 |
| the same + BARRIER_NARROW + BARRIER_DEFER + DISPATCH_INDEP | **+7.6%** | .003 / .007 |

Repeated in later launches: +7.3% (p < .05), +6.6%, and +7.4% with `RX_VK_PIECE_FLUSH=64`. The winning set had
0 black frames in 1844 (Hyrule Field, a shot every frame).

### Translator switches (interleaved launches, Kokiri Forest exit)

| pair | without | with `RX_VK_OOT_GMEM=1` | change |
|---|---|---|---|
| base switches | 29.4 / 31.8 / 30.5 | 33.1 / 32.8 / 32.4 | +7.2% |
| overlap + barrier switches | 31.2 / 34.1 / 32.4 | 35.1 / 35.1 / 34.8 | +7.4% |

Every pair won (6 of 6), with identical draw counts. With `RX_VK_OOT_GM_BASE=1 RX_VK_NO_NAN_GUARD=1` added, plus the
overlap switches and `PIECE_FLUSH=64`: 36.3 / 36.7 / 36.5 fps, against 34.5 / 31.6 / 34.9 for the same switches without
the translator ones (3 of 3). Offline, the two translator switches cut 20 to 57% of the instructions in the culling compute
programs and 15 to 24% in the lit materials (RADV/ACO statistics).

Lake Hylia route, one pass each, frame time normalised to 2400 draws: the plain build 26.5 / 27.4 fps, all of the above
30.1 / 30.2 fps (+12%).

### Level geometry on (mesh fallback)

| place | old fallback (capacity 2048) | `RX_VK_MESH_LEAN=1` |
|---|---|---|
| Kokiri Forest exit, uncapped | 11 fps | 26.5 to 28.3 fps |
| Great Deku Tree B2, 30 fps cap | 15.5 to 19.5 fps | 27 to 29.5 fps |

For comparison: the same spot at the Kokiri exit with every mesh draw dropped runs at ~36.5 fps.
`RX_VK_MESH_NARROW=1` (only the barriers the fallback needs), live A/B in Hyrule Field, 14 rounds, base 23.85 fps:
**+2.7%** (p .001). Picture checked on a cold and on a warm launch.

### Frame pacing

`RX_VK_FRAME_WAIT=2` against the default 1, Lake Hylia route, uncapped, 22 W: 17.1 -> 27.1 fps (+58%, one pass each),
0 black frames in 2274.

## Tried and rejected

- `RX_VK_MESH_ARENA` / `RX_VK_MESH_HOIST` (fallback compute moved ahead of the render pass): +3.0% with NARROW, but the
  arena overflows at 1 GB in Hyrule Field and the draws then fall back to the old path. Not adopted.
- `RX_VK_MESH_RING` (fewer fallback barriers, no fills): +0.3%, not significant.
- `RX_VK_DRY_OVERLAP`: no gain over the overlap set (+8.4% vs +8.6%).
- `RX_VK_ASTC_BC1`: no change. `RX_VK_ASTC_BC_ALL`: -2.5%.
- `RX_VK_EVENT_FLUSH` (worse), `RX_VK_EARLY_SIGNAL`, `RX_VK_FRAME_WAIT_UNLOCKED`, `RX_VK_PRESENT_HANDOFF`, present poll
  step, wake-one, timer slack, order idle, wait nap: within noise.
- Less GPU work by switch: `RX_VK_SHADOW_HALF`, `RX_VK_HALF_PROG`, `RX_VK_LOD_BIAS`, shadow taps, skipping the volumetric
  chain: no measurable gain (the frame was bound by the serial CPU-to-GPU chain, not by GPU work).
- Per-draw CPU micro-optimisations (`RX_VK_TEX_REUSE`, `RX_VK_HIT_FRONT`, `RX_VK_IB_FIRST`, `RX_VK_PICK_UB_FREE`,
  `RX_VK_POOLS_SHARED`, `RX_VK_CHUNK=240`): -5% replay time in the frame bench, +1.7% live (p < .001), and nothing
  significant on top of the overlap set. Left off.
- Builds with profile-guided optimisation, or `-O2` + `-march=znver3` for the cold game code: not faster.
- `RX_VK_ASYNC` (black frames), `RX_VK_PARALLEL=6` (slower), `RX_VK_FAST_SCALE` (no gain), DCC (RADV never compresses
  the GENERAL-layout images this renderer uses).

## Tools

- `RX_VK_LIVE_CFGS=<file> RX_VK_LIVE_GO=<file>`: the live A/B above. Once the `GO` file exists, the game cycles through the
  configurations (`RX_VK_LIVE_ROUNDS`, default 20; `RX_VK_LIVE_SECS`, default 3) and logs
  `vk: live cfg <name>: <presents> presents in <ms> ms, draws <n> (round <r>)`. File format:
  `tools/vega-bench/live-example.cfg`. Summary: `tools/vega-bench/livestat.py <log>`.
- `RX_VK_BENCH_CONFIGS=<file>` with `RX_VK_BENCH=f,<rounds>`: the same configurations replayed on one captured frame.
- `RX_VK_HC_SKIP=<key>[,<key>...]`: the draws and dispatches of those programs left out, as a frame cost map. This changes
  the picture, so use it only for measuring.
- `RX_VK_TL=<path>`: a timeline of the frame's threads and GPU batches, written once `<path>.go` exists.
  `tools/vega-bench/tlan.py <path>` reports GPU busy and idle time and what each thread did while the GPU was idle.

## Defaults that differ from upstream

These are on without any variable, because they fix wrong pictures or GPU hangs on AMD. Each one names the switch that
brings back upstream's behaviour:

- `RX_VK_FRAME_WAIT` defaults to 1: a present waits for the GPU to finish the one before (0: no wait).
- 32-lane warp emulation on GPUs that cannot run 32-wide subgroups (GFX9). `RX_VK_WARP64=0`: off.
- A long submission's fence is signalled by its last part, not its first. `RX_VK_FENCE_FIRST_PART=1`: as before.
- A rendered target is not re-uploaded from a buffer write that overlaps it. `RX_VK_REFRESH_OVER_DRAWN=1`: as before.
- Native BC textures keep only the mip levels whose block grids halve evenly. `RX_VK_BC_TRIM=0`: all levels.
- GPU texture decoding is split into submits of 4 Mtexels each. `RX_VK_UPLOAD_SPLIT_MTEXELS=0`: one submit.
- Indirect task draws in the mesh fallback run every slot. `RX_VK_FB_IND_SLOTS0=1`: as before.
- The mesh fallback capacity is 2048 slots (upstream: 1024). This is a compile-time constant.
- The shader cache stores mesh fallback programs as fallback programs (the speckle fix).
- A 42% step in the Render resolution menu.
