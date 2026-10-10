# Roadmap — after Phase G (2026-10-07)

> **Status: approved on 2026-10-08.** This is the `yae-QindieGL` part of the
> cross-repository program for neural rendering (NR) and path tracing (PT) — the program document is
> `project-empty/docs/NR_PT_Program.md` (decisions P1–P12, contracts C1–C6, milestones M0–M5). The
> user agreed "in general" with the research report behind it
> (`yae-dlss5/reports/Нейрорендеринг и трассировка пути для YAE.md`); on 2026-10-08 the user
> approved the plan and took P1–P12 (P5 and P6 explicitly, the rest as recommended); Phase H's local
> questions Q1–Q10 stay open with their recommendations. Phase G's Remix sessions ran on the RTX 5090
> under Windows; Linux and Windows are one PC; an RTX 3060 Laptop 6 GB is at hand for the RTX 20-30 profile. As in `yae-engine`, each phase
> gets its own plan by the project's method — reconnaissance (numbers, not guesses) → items with their
> source → subphases with an acceptance number → "Done" with the numbers before and after — **when it
> starts**; this file holds only the order, the boundaries and the gates, so that phases do not compete
> for the same items. Phase H is planned now: [`PhaseH_RemixNR.md`](PhaseH_RemixNR.md). Phase
> documents are in Russian, reference documents (this roadmap) in English (P12).

## Principle

- **This repository is tier P of the original game — "path tracing (+ NR)":** *You Are Empty* (32-bit
  OpenGL, DS2 Engine) → QindieGL → Direct3D 9 → the RTX Remix bridge → `dxvk-remix`. Without the
  engine's source, API interception in the Remix style is the only route to real world-space path
  tracing for a closed OpenGL game; everything else is screen-space ReShade effects, which see only the
  depth buffer and the finished image, and every real PT port (RTGL1, Q2RTX, the dhewm3 fork) needs the
  source (research notes, `yae-dlss5/research_notes/Нейрорендеринг и трассировка пути для YAE/legacy_pt_remix.md` §4).
  NVIDIA closed the request for OpenGL through Zink on 2026-02-09 ("would require rewriting
  effectively all of the Remix code", [rtx-remix #978](https://github.com/NVIDIAGameWorks/rtx-remix/issues/978)),
  so a GL → fixed-function D3D9 wrapper stays the way in, and the QindieGL line is the maintained one
  ([whisperglen v1.2.2, 2026-09-27](https://github.com/whisperglen/QindieGL/releases/tag/v1.2.2);
  Quake III Arena RTX runs the same route).
- **Path tracing with authored light and materials is the restoration; NR is opt-in on top (P3).**
  NR is off by default, with an instant A/B toggle, an intensity control, masks for creatures and faces,
  and a dark-scene histogram check. Remix's own NR pass ("DLSS 3D-Guided Neural Generation", `dxvk-remix`
  main since 2026-09-18, [ecf646d](https://github.com/NVIDIAGameWorks/dxvk-remix/commit/ecf646d3ea97b1e81e450993f414a2ae31b71d3b))
  runs after Remix's denoiser and upscaler and before the UI, on Remix's own motion vectors and depth,
  with per-material masks — the inputs an injector into the OpenGL game cannot have.
- **This repository does not replace `yae-dlss5` (P1).** Tier P needs an RT GPU that path traces and
  then pays for NR on top (NR alone ≈ 8 ms per 4K frame on an RTX 5090, Digital Foundry via the report).
  Tier R (`yae-dlss5`) is the only route to NR on RTX 20/30, RDNA2/3, Intel and GPUs without RT, and the
  only one that keeps DS2's own renderer (DS2's shaders, HDR, normal maps — which QindieGL's D3D9 path
  still runs with off: Phase F is deferred). What the tiers share: the DS2 view knowledge (contract C5,
  owned here), `yae-neural` (Phase J), the PBR materials (`yae-materials` → Remix USD).
- **Measure, then change.** Every phase hands in numbers; every estimate is labelled as one; the
  game's resources (`.ds2*`) are never modified.

## Phases so far

| Phase | What | Status |
|---|---|---|
| A | Instrumentation: structured log, crash ring, per-draw dumps, GLIntercept workflow | complete (2026-08-30) |
| B | Startup: DS2's hardware gate passed with the YAE-only `yae_fallback_compatibility` | complete, through the first level |
| C | World rendering: lightmap orientation, video rectangles, fullscreen reset | validated on the reference level |
| D | Stability: 15-minute sessions, safe allocation failures, the LAA patch (2 → 4 GiB) | validated, five locations |
| E | VBOs (CPU shadow storage copied into streaming buffers at draw time) | complete, user-validated |
| F | DS2's ARB shader path (`use_shaders=1`): 58 programs compile, the eye-distance fog fix | **deferred since 2026-09-26** |
| — | Fixed-function path fixes and optimisation: average frame 7.0 → 4.5 ms, p99 26.9 → 8.0 ms | done (`STATUS.md`, "Fixed-function path") |
| G | RTX Remix 1.5.2 bring-up: camera split, camera census, bridge CPU affinity, stable light positions, `rtx.conf` | merged 2026-09-27 (PR #35); **no `STATUS.md` section, no numbers, no record of the levels played — closed by H** |

## Order

| # | Phase | What | Size | Gate (handed in with a number) | Plan |
|---|---|---|---|---|---|
| H | **Remix: close G, measure, main + NR** | `STATUS.md` section for G; a Remix profile with `yae_camera_split = 1` and its regression without Remix; a level-by-level log; texture tagging; a measurement harness; a pinned `dxvk-remix` main with the native NR pass on RTX 50; YAE NR defaults and masks; per-tier `rtx.conf`; stability (frame generation, Sparse Rendering, NRC); contract C5 for tier R | M | G closed with frame times on the five slot scenes and a 24-level log on 1.5.2 and on the pinned main; NR on/off cost measured on RTX 50; the user's verdict on the NR A/B sheet; dark-scene histogram deltas within the agreed thresholds; the QindieGL tests green | [`PhaseH_RemixNR.md`](PhaseH_RemixNR.md) — **plan of 2026-10-07**, H.0–H.11 |
| I | **Authored light and materials** | the texture-identity contract for `yae-materials` M4; M4's Remix USD export with M6.5 de-lighting; DS2's level lamps exported as Remix lights; the baked-light policy (lightmaps, vertex colour); per-level exposure | L | M4's own criterion — "one test room in the original game through the shim + Remix looks better than the base pass" — as a frame A/B with the user's verdict; the frame-time cost against H's numbers measured and inside the budget set at I's start; then level by level | own document at its start; parts can start during H |
| J | **Coverage: the `OpenYAE/dxvk-remix` fork** | the fork at H's pin (P7); FSR 3.1 from the NightSight "Community Edition" draft; NR through `yae-neural` record mode on DXVK's device (after `yae-neural` N3); AMD/Intel validation through the hardware lab; Linux/Proton notes | M–L | FSR 3.1 measured on an AMD GPU; NR through `yae-neural` on RDNA4 at the parity threshold N1/N3 set against NGX on RTX 50, with its cost; at least one lab report per vendor | own document at its start (M3) |
| F | **DS2's shader path** (deferred) | `use_shaders=1`, HDR and normal maps through D3D9 | L (estimate) | native vs QindieGL frames on the five slot scenes with DS2's shaders, HDR and normal maps on, at the native-vs-native noise floor | `STATUS.md` "Phase F"; resumes only if K is triggered or the user asks |
| K | **Contingency: the non-Remix D3D9 frame for tier R's host** | QindieGL (system D3D9, no Remix) hands the pre-HUD frame, depth and camera to `yae-nr-host64` through D3D9Ex shared surfaces | M (estimate) | tier R's own D3 acceptance on the vendor that triggered K, plus the transport's cost in ms | only on its trigger (below) |

**Between the phases.** H comes first and is the only phase planned now: it turns Phase G into a
measured, documented baseline, moves to a `dxvk-remix` main build with NR and fixes the per-tier
profiles. **I runs partly in parallel with H**: its two QindieGL-side pieces — the texture-identity
contract and a prototype export of DS2's level lamps — need neither main nor NR, but I's acceptance
needs H's pinned main and `yae-materials` M4 (contract C6, frozen at M3). **J starts at M3**, after
`yae-neural` N3 delivers record mode (P2) and H has a pin to fork from; its FSR part does not need
`yae-neural` and may start earlier if the user wants AMD image quality first. **F and K wait on
triggers.** The sizes are estimates of effort, not commitments.

```
H.0–H.5  close G on 1.5.2, harness ─┐
H.6a     build main + NR ───────────┼─► H.6b main measured ─► H.7 NR defaults, masks ─► M2
H.10     contract C5 (no game box) ─┘                       ├─► H.8 per-tier profiles
                                                            └─► H.9 stability
I        texture identity · lamp export (parallel) ─► M4 (yae-materials) ─► I acceptance
J        fork at H's pin · FSR 3.1 ─► NR via yae-neural (needs N3) ─► lab reports (P11)
F, K     on trigger only
```

## GPU stack by tier — a hypothesis to be measured

The tiers below come from the research report ("Покрытие видеокарт в режиме Remix строится по
уровням"), built from Remix's options and code and from NVIDIA's DLSS feature tiers. **Nothing in this
table is measured on *You Are Empty*.** Phase H measures the RTX 50 row on the developer's RTX 5090;
every other row waits for the hardware lab (P11).

| Tier profile | GPUs | Indirect light | Denoiser | Upscaler | Frame generation | NR | Main `rtx.conf` keys (H.8) |
|---|---|---|---|---|---|---|---|
| **Portable** | RDNA2/3/4, Arc A/B; fallback for RTX 20/30 | ReSTIR GI, 1–2 bounces | NRD | XeSS 2.1 or TAA-U at `resolutionScale` 0.5–0.67 (FSR 3.1 after J) | off | off; RDNA3/4 through `yae-neural` after J | `rtx.integrateIndirectMode = 1`, `rtx.pathMaxBounces` 1–2, `rtx.upscalerType = 4` (XeSS) or `3` (TAA-U), `rtx.resolutionScale` 0.5–0.67 |
| **RTX 20/30** | Turing, Ampere | NRC | DLSS-RR | DLSS SR | off | impractical (the FP16 build of the leaked DLL) | `rtx.integrateIndirectMode = 2`, `rtx.upscalerType = 1`, `rtx.enableRayReconstruction = True` |
| **RTX 40** | Ada | NRC | DLSS-RR | DLSS SR | DLSS FG 2× | unofficial (a modified DLL, P5 opt-in; never tested under Remix) | as RTX 20/30 + `rtx.dlfg.enable`, `rtx.dlfg.maxInterpolatedFrames = 1` |
| **RTX 50** | Blackwell | NRC (+ Sparse Rendering) | DLSS-RR 4.5 | DLSS SR 4.5 | multi-frame generation | **Remix's native NR** (off by default, P3) | as RTX 40 + `rtx.sparseRendering.enableSparseRendering` (after H.9), `rtx.dlssNeuralRendering.*` YAE defaults |

Every profile also carries the YAE block of Phase G (`tools/remix/rtx.conf`) and
**`rtx.graphicsPreset = 4` (Custom)**: a preset's values sit above `user.conf` and `rtx.conf` in
Remix's layer stack (`documentation/RemixConfig.md:38-60` in `dxvk-remix`), so without Custom the
Auto preset would overwrite the profile. **Frame generation stays off in every profile** until H.9 has
retested the Phase G crash on main.

What is code fact, verified in `dxvk-remix` main at `e4e7303d6d` (2026-10-06) on 2026-10-08:

- NRC is disabled for every vendor other than NVIDIA and below driver 565.90
  (`src/dxvk/rtx_render/rtx_nrc_context.cpp:201-211`); Remix refuses NVIDIA drivers below 610.47 on
  Windows and 525.60 on Linux (`src/dxvk/dxvk_options.cpp:46-50`).
- The Auto preset picks by NVIDIA architecture (Turing Low, Ampere Medium, Ada High, Blackwell Ultra);
  any other vendor gets Low with `resolutionScale` 0.5 and NIS/TAA-U Performance; a GPU with ≤ 8 GB
  drops one step (`src/dxvk/rtx_render/rtx_options.cpp:404-469`).
- The upscalers are None, DLSS, NIS, TAA-U and XeSS — no FSR (`src/dxvk/rtx_render/rtx_options.h:67-73`).
- Sparse Rendering requires DLSS-RR (`RtxOptions.md:746`); DLSS SR/RR 4.5 (310.7.128) arrived on main
  on 2026-08-24 (`7294f68`), after the 1.5.x release branch last took main (2026-05-19).
- Ray-trace modes: TraceRay for indirect light on NVIDIA, RADV and AMD's proprietary driver, RayQuery
  everywhere else, Intel included (`src/dxvk/rtx_render/rtx_options.cpp:592-625`).
- The NR pass writes its output at the display resolution, so its cost grows with the output pixels
  (`src/dxvk/rtx_render/rtx_resources.cpp:1588-1600`).

## Cross-repository dependencies

What this repository needs from others:

| Needed | From | For | When |
|---|---|---|---|
| A pinned `dxvk-remix` main build with the NR pass | NVIDIA (`NVIDIAGameWorks/dxvk-remix`) | H, later J's fork base | H.6 |
| The user's own `nvngx_dlssnr.dll` 310.8.0 (SHA-256 `E16BCF15…1FC8E`, the hash `yae-dlss5` pins) | the user (P4: never in git, CI or a release) | H's NR on RTX 50 | H.6b |
| Record mode (`yn_session_evaluate` into an external `VkCommandBuffer`) and the frame contract C2 | `yae-neural` N3 (C1, C2) | J's NR on AMD/Intel and RTX 20–40 | M3 |
| The Remix USD material export (C6) and de-lighting (M6.5 "Delight") | `yae-materials` M4, M6.5 | I | C6 frozen at M3 |
| DS2's level lamp list (`.ds2` lights: type, position, direction, colour, radii, range, intensity, falloff, shadows) | `yae-engine` loader (`src/assets/DS2Level.h:155-172`) or the `yae-sdk` format library | I's lamp export | I |
| The host `yae-nr-host64` and the YNB bridge protocol v1 (C3) | `yae-dlss5` D3 | K | only on K's trigger |
| GPUs other than the RTX 5090 | the hardware lab (P11) | J, and every non-RTX-50 row above | M2 onwards |

What others need from this repository:

| Provided | To | Contract | When |
|---|---|---|---|
| The DS2 view classification: the camera split rule, projection classes, the world → HUD boundary, the sky camera, the weapon's projection, the 512×512 shadow pass | `yae-dlss5` D2 (glhook) | **C5 — owned here** | H.10, before D2's reconnaissance closes (M2) |
| Remix texture hashes of DS2's textures and how a DS2 texture becomes the bytes Remix hashes | `yae-materials` M4 | input to C6 | H.4 (lists), I (identity contract) |
| The tier-P measurement protocol and report format | the hardware lab (P11), `yae-neural/reports/hw/` | — | H.5 |
| Per-tier `rtx.conf` profiles and the GPU → profile rule | `yae-dlss5` D4 installer (P6) | — | H.8 |

## Milestones

The program document owns the milestones; this is what each one asks of this repository.

| # | Program acceptance (abridged) | This repository's part |
|---|---|---|
| **M0** | P1–P12 decided; `yae-neural` API 0.1 and YNB v1 frozen | the user's answers to P1, P3–P7, P11, P12 and to Phase H's local questions; Phase H approved |
| **M1** | "Phase H closes G and publishes Remix frame times per level" | H.0–H.5: the `STATUS.md` section, the Remix profile, the 24-level log, the tags, the harness numbers on 1.5.2 |
| **M2** | "Remix main + NR measured on RTX 50"; glhook (D2) through a loopback host | H.6–H.10: the pinned main measured against 1.5.2, the NR cost and defaults, the profiles, the stability matrix; C5 delivered to D2 |
| **M3** | tier R on `yae-neural` (D3); N3 modes; "QindieGL J started" | J starts (fork, FSR 3.1, NR through record mode); I's acceptance as soon as C6 is frozen |
| **M4** | after the engine's Release 1: engine NR-3/NR-4, RT-0…RT-2 | I level by level; K only if triggered |
| **M5** | 2027: the student network (N5), the installer with tiers (D4) | the profiles of H.8 used by D4; the student network in tier P through J's fork |

## Risks

| Risk | Effect | Mitigation |
|---|---|---|
| **The NR header package** `rtx-remix-ngx_sdk_dlnr` is in no public source repository (`packman-external.xml:32-34`), and the build includes its headers unconditionally (`rtx_ngx_wrapper.cpp:50-51`) | if NVIDIA removes, renames or locks it, main stops building from public sources | **as of 2026-10-08 it resolves publicly:** packman downloads `rtx-remix-ngx_sdk_dlnr@1.7z` anonymously from NVIDIA's CDN — NVIDIA's own GitHub-hosted CI did so in run 1393 (`e4e7303`) and built green (Phase H, R21). Pin version 1 and its SHA-256, keep the local packman cache on the build machine, keep NVIDIA's CI artifact of the pin (90-day retention); then the fallbacks: the community fork with NR (lunks, 2026-08-29), our own call to NGX with the community-known parameter set, `yae-neural` (J) |
| **`dxvk-remix` main moves fast and takes no pull requests:** 82 commits (42 without merges) in the four weeks to 2026-10-06, ≈ 13 a week over the year; the pulls endpoint answers 404 and issues go to `rtx-remix` (2026-10-08) | behaviour changes between snapshots (camera, hashing, instance preservation); our patches cannot go upstream as pull requests | pin a commit; re-run Phase G's checks and the harness on every re-pin; the fork (J) rebases only at milestones; report bugs on the `rtx-remix` tracker with a repro |
| **The DLSS frame generation crash** (Phase G, Remix 1.5.2, in the NVIDIA driver's `vkQueueSubmit`) | no FG on RTX 40/50, a large part of the NVIDIA tiers' value | FG off in every profile; main carries two FG fixes that 1.5.2 lacks (`7e50b74` 2026-06-01, swapchain image count vs the FG multiplier; `1f10dd6` 2026-08-05, multi-frame above 2×) — candidates, retested in H.9 with the crash dump kept |
| **Non-NVIDIA presets and features:** Auto drops any non-NVIDIA GPU to Low; NRC, DLSS-RR and Sparse Rendering are NVIDIA-only; no FSR; an open RDNA2 artifact/slowdown issue ([#914](https://github.com/NVIDIAGameWorks/rtx-remix/issues/914)); HL2 RTX did not run on Arc in June 2026 (PCGH) | tier P on AMD/Intel is the raw path-tracing cost with the weakest upscalers | explicit per-tier profiles (H.8); FSR 3.1 and NR through `yae-neural` in J; AMD/Intel claims stay "hypothesis" until a lab report (P11); Arc treated as "may not start" until tested on the pin |
| **DS2's lamps never light the static geometry:** `lightmapped_base` and `vertlight_base` are drawn with `glDisable(GL_LIGHTING)`; the lamps' light is in the lightmap (`yae-engine/docs/Invariants.md:5526-5528`), and DS2 passes GL lights only near lit models | the light set Remix sees is incomplete by construction; with lightmaps ignored the world is lit by the sky and stray model lamps | Phase I: DS2's level lamps exported from the level files as Remix lights, the baked-light policy, de-lighting (M6.5) before PBR materials meet the path tracer |
| **NR sees a darker image than the player** (inference from code, not measured): Remix builds NR's SDR proxy with `rtx.tonemap.exposureBias` (1.0 when auto-exposure is off), while YAE's picture comes from the local tone mapper at `rtx.localtonemap.exposure` 5.3 | in a dark game NR would work on a frame ≈ 2.4 EV darker than the one displayed | measured in H.7 on the five slot scenes; then `rtx.tonemap.exposureBias` aligned in the NR profile only |
| **One leaked NR runtime:** every NR route pins `nvngx_dlssnr.dll` 310.8.0; NVIDIA announced model updates "later this fall" | a new official DLL may change behaviour or the parameter set | NGX takes official DLLs as they come; pin by hash (P4); `yae-neural` and the student network (P10) end the dependency |
| **Reception of NR** as an "AI filter" (58–71 % against in press polls; a dark horror game is the worst case) | backlash | P3 everywhere: off by default, A/B, intensity, masks, histogram checks; PT with authored materials is the restoration |
| **The CPU side under the bridge:** DS2 is single-threaded and pinned to CPU 0; the bridge serialises every D3D9 call into another process (without Remix: 507–980 draws per frame on average over the profiling routes, ≈ 2150 draws and 370 000 vertices in the heaviest location, `STATUS.md:782-798`) | tier P may be CPU-bound on any GPU | measured in H.5 (QindieGL's own `PERF` lines against Remix's GPU numbers); the deferred static VBOs (`STATUS.md:812-817`) are the lever |
| **Only an RTX 5090 at hand** | every non-RTX-50 statement stays unmeasured | the hardware lab (P11); no profile becomes a default on a GPU line without a report from that line |

---

## H — Remix: close G, measure, main with NR

**Goal.** Close Phase G the way phases close in this project — a `STATUS.md` section, a profile without
manual steps, a level-by-level log and numbers — and then move tier P to a pinned `dxvk-remix` main build
with Remix's native NR pass on RTX 50, measured, with YAE defaults, masks and per-tier profiles.

**Scope** (subphases H.0–H.11 in [`PhaseH_RemixNR.md`](PhaseH_RemixNR.md)):

- **Close G on Remix 1.5.2:** the test bench recorded (GPU, driver, hashes); the Remix profile with
  `yae_camera_split = 1` and its in-game regression without Remix (today the shipped INI has 0 while the
  README asks for 1, `msvc/QindieGL.ini:187,205` vs `tools/remix/README.md:79`); the DS2 settings for
  Remix as files; the `STATUS.md` section; a log of all 24 campaign levels; the texture tagging (decals,
  lightmaps, sky, particles, UI, water).
- **A measurement harness:** the five retail slot scenes the engine already uses (`Save78` med1, `Save60`
  wall, `Save77` kolhoz_part2, `Save74` met6, `Save67` lastzlo), loaded with `-exec_cmd "smap SaveNN"`;
  Remix's own `metrics.txt` and pre-UI screenshots through its environment hooks; QindieGL's `PERF`
  lines; PresentMon on the bridge server; a noise floor from a repeated run; the same protocol packaged
  for the hardware lab.
- **Main with NR:** pin a CI-green main commit (today `e4e7303d6d`, 2026-10-06; at least `c4165bc`,
  2026-10-05, which fixed the NR output aliasing a freed texture), build it from source, cross-check with
  NVIDIA's CI artifact of the same commit, re-run G's checks and the harness, put the user's NR DLL into
  `.trex\`, measure NR on/off.
- **YAE NR defaults and masks:** the proxy exposure aligned; model and intensity by the user's verdict on
  an A/B sheet; the dark-scene histogram check; per-material masks for creatures and faces as a USD mod
  (`rtx-remix/mods/…/mod.usda`, mask-only `mat_<hash>` overrides — the game's draws reach Remix
  replacements only through USD; the Remix API's material masks apply to API meshes only); fog through
  the volumetric control mask; an instant A/B key.
- **Per-tier `rtx.conf`** (the table above) and their selection (`Set-YAEDllChain.ps1 -Profile` now;
  `yae-dlss5` D4 later, P6).
- **Stability:** DLSS FG on main, Sparse Rendering, NRC against ReSTIR GI.
- **Contract C5** for tier R's glhook: an English reference of the DS2 view classification, pointing at
  the code and the tests.

**Gate.** G is closed: the `STATUS.md` section, 24/24 levels in the log, frame times for the five slot
scenes on 1.5.2 and on the pinned main with their noise floor; NR on/off cost measured on RTX 50; the
user's verdict on the NR A/B sheet; histogram deltas within the thresholds agreed in H; `QindieGL_Tests`
green including `yae-camera-split`, and the in-game frame without Remix unchanged by the camera split at
the noise floor. **Size** M. **Lane** D of the program (the Windows game box with an RTX 50).

---

## I — Authored light and materials

**Goal.** Give Remix the light DS2 bakes and the materials DS2 never had, without lighting the 2006
textures twice.

**Scope.**

- **The texture-identity contract (QindieGL's side of M4's first item, "map texture paths to the IDs
  Remix sees through the shim").** Remix identifies a game texture by the XXH3-64 hash of its mip 0 as
  handed to D3D9 (`src/d3d9/d3d9_common_texture.cpp:666-686` in `dxvk-remix`) — the same hashes as the
  14 decal tags of Phase G. QindieGL documents how a DS2 texture becomes those bytes (format
  conversions, mip generation, rectangle textures, runtime-built lightmap atlases) and adds a diagnostic
  dump "GL texture → size, format, Remix hash" for the golden levels, so that `yae-materials` can compute
  the IDs offline from its `match.sha1`-identified sources and verify them against the dump.
- **`yae-materials` M4 — the Remix USD export (C6)** with **M6.5 — de-lighting** (`yae-materials/PLAN.md:159-167,257-292`):
  `mod.usda` material overrides `mat_<hash>` with PBR maps, normal maps, the NR mask fields that H's
  pilot introduced, and the baked-light policy; written directly from the catalog's records, without a
  shared intermediate format (`PLAN.md:436-446`).
- **Authored lamps.** The level files carry DS2's full lamp list (`yae-engine/src/assets/DS2Level.h:155-172`);
  export each level's lamps as Remix lights so the static world gets the light DS2 baked into its
  lightmaps; decide what happens to the GL lamps QindieGL forwards for lit models (`rtx.ignoreLights`,
  or keep them for the models); lamp fixtures as emissives.
- **The baked-light policy:** lightmap textures (`rtx.lightmapTextures`), vertex colour as baked
  lighting (`rtx.ignoreBakedLightingTextures`), per-level exposure (Remix Logic dynamic layers, or the
  per-map options upstream QindieGL already pushes through `SetConfigVariable`, `code/rmx_gen.cpp:98-160`,
  which need a map-name source and Remix API headers at the runtime's minor version).

**Gate.** M4's own criterion on one room first — "one test room in the original game through the shim +
Remix looks better than the base pass" — as a frame A/B on a slot scene with the user's verdict, and
the frame-time cost against H's numbers measured and inside the budget set at I's start; then level by
level, with the log of H as the regression list. **Size** L. **Depends on** H (the pin), `yae-materials` M4 and M6.5 (C6, frozen at M3).

---

## J — Coverage: the `OpenYAE/dxvk-remix` fork

**Goal.** Take tier P past NVIDIA's lock where the code allows: FSR 3.1 for AMD and Intel, NR on AMD,
Intel and RTX 20–40 through `yae-neural`, and a validated, documented Portable profile.

**Scope.**

- **The fork** (P7) at H's pin; CI like upstream's — NVIDIA's public workflow builds on GitHub-hosted
  `windows-2022` runners (`.github/workflows/build.yml`), so the fork's CI needs no private credentials;
  rebase only at milestones.
- **FSR 3.1** from the NightSight "Community Edition" draft ([a2ec0f4](https://github.com/NightSightProductions/RTX-Remix-Community-Edition/commit/a2ec0f42c592e6783948b09717ac5a97ba2d3c94),
  2026-03-20: 35 files, +3720/−47, FSR 3.1 upscaling, FSR frame generation, RCAS, a FidelityFX-SDK
  submodule; the repository has not moved since). It was written against a March main, so this is a
  port onto the pin, not a cherry-pick.
- **NR through `yae-neural` record mode on DXVK's device** (after `yae-neural` N3). The call site is
  `NGXNeuralRenderingContext::evaluateNeuralRendering` (`src/dxvk/rtx_render/rtx_ngx_wrapper.cpp:1060-1115`),
  fed by `DlssNeuralRendering::dispatch` (`rtx_dlss_neural_rendering.cpp:78-216`): colour = the fast
  tone-mapped SDR proxy (`YN_COLOR_LDR_PROXY` in C2), motion vectors in render pixels, depth, the RGBA8
  material control mask, the history reset — recorded into DXVK's command buffer as NGX is today. The
  support gate that hides the NR menu without NGX (`dxvk_imgui.cpp:3645`) widens to "NGX or `yae-neural`".
- **AMD/Intel validation** of the Portable profile and of the fork through the hardware lab (P11):
  RDNA2's open issue #914, RDNA3/4, Arc (treated as "may not start" until tested).
- **Linux/Proton notes:** Remix's Linux driver floor (525.60, `dxvk_options.cpp:50`); the open
  [#1101](https://github.com/NVIDIAGameWorks/rtx-remix/issues/1101) — DLSS FG hangs at start and a
  500 ms Reflex stall per frame through dxvk-nvapi under GE-Proton (REMIX-6202 filed 2026-10-05); QindieGL
  and the x86 bridge under Wine/Proton, and DS2's CPU pin there, are untested.

**Gate.** FSR 3.1 selectable and measured (frame time, the user's look check) on at least one AMD GPU;
NR through `yae-neural` on RDNA4 at the parity threshold N1/N3 set against NGX on RTX 50 (the program's
proposal: ≥ 45 dB), with its cost in ms; at least one lab report per vendor; Linux: one documented run or
a list of blockers. **Size** M–L. **Depends on** `yae-neural` N3 (C1, C2), H's pin.

---

## F — DS2's shader path (deferred)

**Goal.** Run DS2's own ARB shader path (`use_shaders=1`, HDR, normal maps) through D3D9 at native
parity.

**State.** Deferred since 2026-09-26: the observed corpus compiles (58 programs, 0 failures), DS2's
weapon-fog bug is fixed behind `yae_eye_distance_fog`; HDR and normal maps were never brought up
(`STATUS.md:621-714`).

**Why it stays deferred, and why it still matters.** Tier P does not need it: Remix replaces DS2's
shading with path tracing, and draws through ARB vertex programs would depend on Remix's vertex capture
with geometry hashes that change with animation — Remix runs keep `use_shaders=0`. It matters for **tier
R's look parity**: tier R is DS2's own renderer with its shaders, HDR and normal maps; if the original's
frame ever has to go through QindieGL's D3D9 path without Remix (Phase K), that frame would lose them
and tier R would look different on the vendor that needed K.

**Gate.** Native vs QindieGL frames on the five slot scenes with `use_shaders=1`, HDR and normal maps on,
differing by no more than two native runs differ. **Resume trigger:** K is triggered, or the user asks
for DS2's shaders under D3D9 tools.

---

## K — Contingency: the non-Remix D3D9 frame for tier R's host

**Goal.** A fallback transport for tier R, should its OpenGL interop fail on a vendor.

**Trigger.** `yae-dlss5` D2/D3 report that glhook's GL interop (`GL_EXT_memory_object_win32` /
`GL_EXT_semaphore_win32` importing D3D12 resources and fences — confirmed on NVIDIA and on AMD Adrenalin
26.8.1, unconfirmed in Intel's Windows OpenGL driver) fails on a vendor, and YNB's CPU-copy fallback
misses its frame budget there.

**Scope.** QindieGL without Remix creates a Direct3D 9Ex device (today `Direct3DCreate9`,
`code/d3d_global.cpp:1025`); at the world → HUD boundary — which the view diagnostics find only after
the frame today (`code/d3d_view_diagnostics.cpp:559-582`), so K moves the decision into the frame — it
copies the pre-HUD colour, and depth through an INTZ texture, into D3D9Ex shared surfaces; the camera
matrices the camera split already separates go into YNB's control block; the host opens the shared
handles (D3D9Ex surfaces open in D3D11, Microsoft's
[surface sharing between Windows graphics APIs](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/surface-sharing-between-windows-graphics-apis)),
synchronised through event queries and the control block (D3D9Ex has no shared fences); the HUD is drawn
after the host's composite. Phase F decides whether that frame matches tier R's look.

**Gate.** Tier R's own D3 acceptance on the vendor that triggered K — colour before the HUD and camera
motion vectors (reprojection error: with < without < opposite sign) — plus the transport's cost in ms.
**Depends on** `yae-dlss5` D3 and YNB v1 (C3).
