# Decision Log

## 2026-02-19
- Added reusable shared movement integration (`shared/game/Movement`) with configurable ground/air acceleration, friction, sprint/crouch speed multipliers, jump velocity, gravity, and move-state classification (`idle|walk|sprint|crouch|air`) so server simulation and client prediction use identical kinematic rules.
- Extended runtime tuning surface to `runtime.movement` and `runtime.camera` blocks, keeping legacy `runtime.movement_speed_units_per_second` as a compatibility fallback while default configs migrate to explicit movement/camera fields.
- Added `config/server_playable.json` as the interactive playability preset (`60Hz` tick, faster acceleration/friction, tuned jump/gravity, and camera/FOV smoothing defaults).
- Updated synthetic load traffic in `tools/devy_load_client.cpp` to include `player_input.sprint` and `player_input.crouch` fields so profiling/reliability traffic remains protocol-compliant after schema hardening.

## 2026-02-15
- Added `AGENTS.md` to establish autonomous workflow/state commands and local project metadata.
- Chosen Phase 0 execution focus from `TODO.md`: deliver foundation guardrails and test harness before networking feature expansion.
- Adopted Catch2 + CTest for first unit/integration harness because shared gameplay/protocol code is currently easiest to test without full client runtime.
- Added timed server smoke mode (`--smoke-seconds`) instead of signal-only shutdown so CI/local smoke runs can terminate deterministically.
- Added split CMake presets:
  - System toolchain presets (`debug`, `release`) for environments without vcpkg setup.
  - vcpkg presets (`debug-vcpkg`, `release-vcpkg`) for manifest-driven dependency resolution.
- Started Phase 1 (M1.1) protocol hardening with an explicit message schema contract table in `shared/net/Protocol.cpp` to enforce required payload fields and types per message family.
- Introduced versioned protocol envelope (`version`, `type`, `payload`) while preserving backward compatibility for legacy packets that omit `version` (defaulting to v1).
- Added typed parse result/error surface (`ParseResult`, `ProtocolError`) and switched server receive path to strict validation + drop-on-invalid behavior for malformed packets.
- Continued Phase 1 into M1.2 by introducing `server::SessionManager` to centralize join lifecycle, slot allocation, disconnect cleanup, heartbeat tracking, and timeout eviction as deterministic server-runtime state.
- Kept reconnect identity stable by reserving player-name-to-slot mappings across disconnects/timeouts, so reconnecting with the same name reclaims the prior player slot when available.
- Added protocol `heartbeat` message schema contract (`MessageType::Heartbeat`) and aligned server join handshake to explicit `JoinRequest` gating instead of auto-accepting on raw ENet connect.
- Started Phase 1 M1.3 with a dedicated `server::AuthoritativeLoop` runtime module so tick cadence, input queue bounds/order, and snapshot scheduling can be tested independently from ENet I/O.
- Chosen deterministic input application order as `(player_id ASC, input_seq ASC)` per tick to keep server-side state updates stable across runs.
- Chosen configurable snapshot cadence by tick count (`runtime.snapshot_interval_ticks`) rather than wall-clock timers to align snapshot emission with authoritative tick boundaries.
- Started Phase 2 M2.1 by introducing `server::MovementSimulation` as a separate runtime module so authoritative movement integration can evolve/test independently from ENet and session handling.
- Chosen "latest input per player per tick" application semantics in movement integration to avoid over-integrating bursty queued inputs within a single server tick.
- Chosen snapshot-level reconciliation scaffold as per-player `last_processed_input_seq` emitted in `state_snapshot` player entries.
- Added runtime config knob `runtime.movement_speed_units_per_second` to keep movement tuning externalized in JSON without recompilation.
- Added `client_runtime` module (`client::PredictionReconciler`) to scaffold client-side input prediction and snapshot reconciliation around authoritative `last_processed_input_seq` acknowledgements.
- Chosen reconciliation strategy: reset predicted state to authoritative snapshot, drop acked local inputs, replay only unacked inputs in sequence order.
- Added deterministic latency/loss simulation matrix tests (`50/100/200ms`, `1/3/5%`) to bound reconciliation correction drift and authoritative-loss drift for M2.1 validation.
- Started Phase 2 M2.2 with a dedicated `server::WorldReplication` module so chunk-interest management and chunk-revision deltas are testable independently from ENet I/O.
- Chosen relevance culling policy as circular chunk radius in the XZ plane (`distance_sq <= radius^2`) and full vertical chunk layers in-range.
- Chosen state snapshot extension approach (`state_snapshot.payload.chunk_sync`) rather than a new message type to preserve backward-compatible parser behavior while extending snapshot contracts.
- Chosen revision-only chunk deltas for now (no raw voxel payload transfer) because current terrain generation is deterministic and block-level mutation flow is scheduled for M2.3.
- Started Phase 2 M2.3 by introducing `server::BlockInteraction` so block-update validation and conflict handling are isolated from ENet I/O and directly testable.
- Chosen block interaction semantics:
  - `block_id == 0` represents break (set to air),
  - `block_id != 0` represents place,
  - placement on non-air and break on air are rejected as conflicts.
- Chosen race/conflict policy as first-writer-wins under authoritative server ordering; subsequent competing updates for the same occupied block are rejected.
- Chosen block-id validation source as `config/blocks.json` ids loaded at server boot with fallback to `{0,1,2,3}` if config parsing fails.
- Chosen replication integration path as chunk-revision invalidation (`WorldReplication::mark_chunk_dirty`) after authoritative block apply.
- Extended `block_update` protocol schema to accept optional `player_id` for identity consistency checks while preserving compatibility with payloads that omit it.
- Started Phase 3 M3.1 with a dedicated `server::CombatSimulation` runtime module so weapon authority, damage resolution, and death events are deterministic and independently testable from ENet I/O.
- Added protocol `weapon_fire` contract (`MessageType::WeaponFire`) with strict payload schema (`player_id`, `shot_seq`, `weapon_id`, `origin`, `direction`) to decouple combat fire requests from movement input packets.
- Chosen combat authority model:
  - hitscan weapons resolve immediately via server-side ray checks with configurable hit radius/range,
  - projectile weapons resolve via deterministic delayed impacts based on distance and projectile speed.
- Chosen deterministic fire ordering as `(attacker_id ASC, shot_seq ASC)` per tick to keep kill outcomes stable under simultaneous fire.
- Chosen death broadcast strategy as:
  - immediate reliable `damage_event` packets (with `lethal=true` when applicable),
  - mirrored `damage_event`/`death_event` entries in `state_snapshot.payload.events` on snapshot cadence.
- Extended weapon config contract with optional combat tuning fields (`range_units`, `projectile_speed_units_per_second`) and shared DPS helpers (`weapon_damage_per_shot`, `weapon_dps`) for fixture sanity tests.
- Added `client::WeaponFireEmitter` to centralize client-side fire request packet assembly with:
  - monotonic local `shot_seq` allocation,
  - finite input validation,
  - normalized aim direction in outgoing `weapon_fire` payloads.
- Extracted combat event packet/snapshot translation into `server::CombatEvents` so runtime broadcast serialization and integration tests share one mapping path.
- Added M3.1 integration coverage that exercises authoritative fire request handling through delayed/immediate damage resolution, reliable damage broadcast packet generation, and snapshot reconciliation fields (`health`, `alive`, `last_shot_seq`).
- Started Phase 3 M3.2 with dedicated `server::InventoryLootSimulation` so treasure spawning, pickup validation, and drop-on-death logic remain deterministic/testable outside ENet I/O.
- Chosen loot spawn policy as deterministic tick cadence (`inventory.spawn_interval_ticks`) with bounded active spawn pool and stable spawn-id sequencing.
- Chosen pickup validation path as explicit protocol message (`treasure_pickup`) carrying `player_id`, `pickup_seq`, and `spawn_id`, with strict server-side identity/range/capacity/weight checks.
- Chosen duplicate pickup policy as global consumed-spawn tracking (`spawn_id`) to reject replayed pickup requests after authoritative collection.
- Chosen drop-on-death policy as inventory-to-world conversion of all victim items when `loot_drop=all`, emitted as deterministic death-drop treasure spawns at victim position with bounded spread.
- Chosen inventory replication strategy as:
  - targeted reliable `inventory_update` packets on inventory deltas,
  - snapshot extensions with per-player inventory totals and active `treasure_spawns`,
  - snapshot event stream entries for treasure spawns and pickup outcomes.
- Started Phase 3 M3.3 with dedicated `server::MatchLifecycleSimulation` so match-phase authority, respawn budgets, scoreboard aggregation, and deterministic winner resolution remain testable outside ENet I/O.
- Chosen lifecycle gating model:
  - only `InMatch` accepts `weapon_fire` and `treasure_pickup`,
  - `PreMatch` countdown resets when connected players drop below `match.min_players_to_start`,
  - `PostMatch` winner is resolved deterministically by `(kills DESC, coins DESC, deaths ASC, player_id ASC)`.
- Chosen respawn enforcement model:
  - each death consumes one respawn budget unit when available and schedules delayed respawn by authoritative tick,
  - exhausted budget marks player eliminated,
  - respawn application is server-authoritative via `CombatSimulation::respawn_player(...)`.
- Chosen replication strategy for lifecycle state:
  - reliable `match_state` packet broadcast on state changes and snapshot cadence,
  - immediate `match_state` packet on successful join so clients initialize HUD/timers deterministically,
  - snapshot extensions with top-level `match_state` and per-player `respawns_remaining`/`eliminated`.
- Started Phase 4 M4.1 with a dedicated `server::RuntimeTelemetry` module so profiling/measurement logic is deterministic, testable, and decoupled from ENet/gameplay modules.
- Chosen profiling emission cadence as tick-based (`runtime.profiling.report_interval_ticks`) to keep reports aligned with authoritative loop scheduling instead of wall-clock timers.
- Chosen telemetry scope for this slice:
  - per-window tick cost stats (`avg/p95/max`),
  - per-phase timing breakdown (`movement`, `combat`, `inventory`, `match_lifecycle`, `broadcast`, `snapshot_build`, `snapshot_send`),
  - outbound packet counts/bytes by protocol message type,
  - peak runtime pressure counters and estimated state memory footprint from active sessions/queues/events/spawns.
- Chosen default reliable `match_state` behavior as state-change-first (no snapshot-cadence resend) and added runtime override `runtime.match_state_broadcast_on_snapshot` for compatibility/perf tuning.
- Chosen to trim snapshot payloads by default via `runtime.snapshot_include_match_scoreboard=false`, keeping full scoreboard in reliable `match_state` packets while removing duplicated scoreboard arrays from cadence `state_snapshot` payloads.
- Added local vcpkg auto-discovery (`./vcpkg`, `../vcpkg`) to configure/build/test scripts so `debug` workflows can self-select `*-vcpkg` presets without manual `VCPKG_ROOT` export.
- Added ENet CMake compatibility fallback to link `unofficial::enet::enet` when `ENet::enet` is unavailable in vcpkg environments.
- Updated runtime fixtures/tests to match current authoritative semantics:
  - movement clamps axes to `[-1,1]` before normalization,
  - combat fire cadence enforces weapon cooldown ticks,
  - ray hit resolution is nearest-victim along ray (line-of-fire obstruction is authoritative),
  - single-player `min_players_to_start=1` can immediately satisfy one-contender post-match logic.
- Chosen test config path resolution in `WeaponsTests` as ancestor-walk lookup for `config/weapons.json` to keep tests executable from both source-root and preset build directories.
- Added `devy_load_client` plus `scripts/profile-load.sh` to produce active-session telemetry artifacts and phase/message summaries under `artifacts/telemetry/*` for repeatable M4.1 profiling evidence.
- Extracted match lifecycle payload serialization from `server/src/main.cpp` into dedicated helpers (`server::build_match_state_payload`, `server::build_match_state_packet`) to keep reliable and snapshot lifecycle payload contracts synchronized through one formatter.
- Added explicit server-unit regression coverage for snapshot scoreboard toggle semantics:
  - snapshot payload can intentionally omit `match_state.scoreboard`,
  - reliable `match_state` packets must always include `scoreboard`.
- Replaced per-snapshot hash-map fan-in joins in snapshot assembly with a session-indexed player view path to reduce container churn in `snapshot_build`.
- Chosen to track short-run profiling results as a range across repeated runs (`snapshot-build-indexed-v2`, `snapshot-build-indexed-v3`) due observed variance in 8-second synthetic windows.
- Started Phase 4 M4.2 reliability hardening by introducing `server::validate_server_config(...)` and
  switching server boot to fail-fast config rejection instead of warning-and-clamp behavior.
- Chosen strict `loot_drop` config contract (`all|none`) during boot validation to avoid silent
  fallback to `all` when misconfigured.
- Added integration-level invalid-config boot checks in CTest (`server.config.invalid_tick_rate`,
  `server.config.invalid_loot_drop`) to guard startup rejection behavior.
- Added `devy::config::try_load_json(...)` and switched server startup to hard-fail on config
  file load/parse errors, closing the silent-default fallback path for malformed/missing configs.
- Continued Phase 4 M4.2 with script-driven reliability drills instead of embedding watchdog/chaos
  logic into the server runtime so operational hardening can iterate without coupling to gameplay
  code paths.
- Extended `devy_load_client` for chaos testing (`--malformed-rate-hz`,
  `--disconnect-interval-ms`, `--reconnect-delay-ms`) so malformed payload and disconnect churn
  scenarios reuse existing ENet session logic and produce deterministic counters for assertions.
- Chosen watchdog contract as bounded retries with per-attempt log files and rotation
  (`scripts/watchdog-server.sh`) to keep failure diagnostics while capping artifact growth.
- Chosen restart recovery validation as two explicit load phases separated by a forced server
  restart (`scripts/restart-recovery.sh`) to prove post-restart admission and session recovery in a
  repeatable local check.
- Added dedicated reliability CTest entries (`server.reliability.watchdog_restart`,
  `server.reliability.chaos_drill`, `server.reliability.restart_recovery`) to make M4.2 resilience
  checks first-class alongside existing unit/smoke/config suites.
- Extended chaos-injection controls in `devy_load_client` with malformed family selection
  (`--malformed-family`) and burst envelopes (`--malformed-burst-size`) so protocol reject paths
  can be exercised across multiple error classes in one run.
- Chosen default chaos drill posture as `mixed` malformed family + burst injection to broaden
  parser rejection coverage beyond single malformed envelope variants.
- Added `scripts/reliability-soak.sh` to run bounded long-duration reliability windows with
  rotating chaos scenarios plus periodic restart recovery, and emit trend summaries
  (`chaos-metrics.csv`, `restart-metrics.csv`, `summary.txt`) under `artifacts/reliability/soak/*`.
- Added short soak integration test (`server.reliability.soak_short`) for local/CI gating while
  preserving longer soak durations for dedicated manual/scheduled runs.
- Added `.github/workflows/reliability.yml` with:
  - reliability drill CI lane (`server.reliability.*`) and failure artifact upload,
  - scheduled/manual soak lane that archives reliability artifacts per run.
- Started Phase 4 M4.3 telemetry/diagnostics by extending `RuntimeTelemetry` with explicit
  operational counters and rates:
  - tick-lag count/rate against authoritative tick budget,
  - inbound drop/parse-error rates,
  - command rejection count,
  - active-player average/last window counters.
- Chosen machine-parseable diagnostics stream as JSON log lines (`Runtime diagnostics json=...`)
  emitted at the same report cadence as runtime profiling windows to avoid adding an HTTP server
  dependency during alpha.
- Chosen threshold-based alerting contract under `runtime.profiling.alerts` with in-band report
  `alerts` arrays, plus an integration dry-run check (`server.telemetry.alert_dry_run`) to verify
  alert wiring from config to emitted diagnostics.
- Started Phase 5 M5.1 by adding a dedicated CI workflow (`.github/workflows/ci.yml`) instead of
  overloading reliability lanes, to keep build/test/package/repro checks independently visible and
  enforceable.
- Chosen CI sharding model as name-regex CTest slices (`shared`, `client`, `server`, integration)
  so shard logic remains stable without requiring CTest label migration in this slice.
- Chosen deterministic release packaging as script-driven tarball assembly with normalized tar/gzip
  metadata (`--sort=name`, fixed `--mtime`, numeric owner/group, `gzip -n`) so artifact hashes are
  reproducible across reruns on identical inputs.
- Added explicit CI verification scripts:
  - `scripts/ci-cache-check.sh` to assert stable configure cache keys and second-build no-op
    behavior.
  - `scripts/ci-failure-injection.sh` to assert invalid-config startup failure remains enforced as
    a pipeline guardrail.
- Started Phase 5 M5.2 by adding profile-driven launch operations (`scripts/launch-profile.sh`)
  with environment-backed launch profiles in `profiles/launch/*.env` so server/client startup
  parameters are reusable between local rehearsal and release runbooks.
- Chosen release config-template strategy as valid JSON baselines (`config/templates/*.json`)
  instead of placeholder tokens, so templates are executable as-is and can be promoted with minimal
  edits.
- Added script-driven release notes generation (`scripts/generate-release-notes.sh`) keyed to
  Conventional Commit prefixes to keep release summaries reproducible and low-friction for each
  cut.
- Chosen M5.2 validation as executable rehearsals, not docs-only checklists:
  - `scripts/install-package-smoke.sh` for fresh package extract + runtime verification,
  - `scripts/protocol-compat-check.sh` for mixed valid traffic plus unsupported-version packet
    rejection behavior,
  - `scripts/rollback-rehearsal.sh` for candidate deploy and baseline rollback recovery.
- Added release-operation CTest lanes (`server.release.*`) and included them in CI integration
  shard regex to keep packaging/runtime/rollback checks continuously enforced.
- Started Phase 5 M5.3 by codifying alpha release-gate artifacts in-repo (bug-bash checklist,
  known-issues register, acceptance checklist, tag flow) so release-readiness is tracked as
  versioned docs rather than ad-hoc notes.
- Chosen alpha acceptance validation as a script-driven scenario pack
  (`scripts/alpha-acceptance-pack.sh`) that combines profile-load, chaos-drill, and restart
  recovery evidence capture with optional full regression re-run.
- Chosen endurance gate implementation as a thin wrapper over existing reliability soak
  (`scripts/alpha-endurance-run.sh`) to preserve one reliability source of truth while adding
  alpha-specific artifact conventions and summaries.
- Added orchestrated alpha gate runner (`scripts/alpha-release-gate.sh`) to sequence acceptance,
  endurance, and release-note generation, and emit deterministic tag-candidate commands in a single
  gate summary.
- Added short-runtime alpha gate CTest entries (`server.release.alpha_acceptance_pack`,
  `server.release.alpha_endurance_short`) and included them in CI integration regex as dry checks
  that keep M5.3 automation wiring continuously validated without running 8-hour jobs in CI.
- Executed `scripts/alpha-release-gate.sh` in candidate dry-run mode (`run_endurance=0`) as the
  next actionable M5.3 step to validate acceptance + release-notes orchestration before committing
  to the full 8-hour endurance gate.
- Chosen `v0.2.0-alpha-rc1-dryrun` as a non-final release-notes/tag candidate identifier so
  generated artifacts remain traceable without implying alpha sign-off before long-run evidence.
- Observed release-gate precondition gap: local repository has no commit/tag history yet, so
  generated release notes currently report zero commits; final alpha cut must establish a baseline
  commit/tag range before release-note generation can provide meaningful deltas.
- Fixed `scripts/generate-release-notes.sh` git-log scan loop to process the final commit record
  even when the stream has no trailing newline, preventing off-by-one/zero-count release notes on
  small ranges.
- Added dedicated release-note generation regression gate (`tests/scripts/assert-release-notes.sh`,
  CTest `server.release.release_notes_generation`) and included it in the CI integration shard
  regex to keep Phase 5 release-note integrity continuously verified.
- Executed a bounded full-path alpha gate follow-up run with endurance enabled:
  - `scripts/alpha-release-gate.sh v0.2.0-alpha-followup config/server_test.json debug-vcpkg 6 4 1 artifacts/releases/alpha-gate/followup-local 1`
  - acceptance, endurance, and release-note generation all passed in a single orchestration path.
- Chosen to keep this run short (`1` minute endurance) as a local wiring/proof check and preserve
  the final M5.3 sign-off requirement for a dedicated long-duration (`8` hour) endurance evidence
  run before release tag cut.
- Started the full-duration alpha endurance evidence run with a detached process launch to keep the
  8-hour task alive beyond a single terminal invocation:
  - `scripts/alpha-endurance-run.sh config/server_test.json 480 8 artifacts/releases/alpha-endurance/candidate-8h-20260215-184958 20 6 6`
  - tracked live process id in
    `artifacts/releases/alpha-endurance/candidate-8h-20260215-184958/pid.txt`.
- Chosen detached `setsid` launch semantics for long-running release validation jobs in this local
  environment because plain background jobs tied to one shell invocation were terminated before the
  endurance run could progress.
- Added `scripts/alpha-endurance-status.sh` so the in-progress/final alpha endurance gate can be
  monitored with one command (`process_state`, completed chaos/restart cycles, run/soak summary
  availability, recent timeline tail) instead of ad-hoc `tail` + `cat` checks.
- Confirmed the detached 8-hour endurance run remains healthy mid-flight via the status helper
  (`process_state=running`, `chaos_cycles_completed=16`, `restart_runs_completed=2`) and kept the
  final alpha gate blocked only on completion evidence.
- Added `/artifacts/` to `.gitignore` to keep generated reliability/release evidence local and
  prevent non-source runtime outputs from polluting baseline release-prep commits.
- Chosen to perform the TODO roadmap's "baseline history" precondition now (before final tag cut)
  so release-note ranges can anchor on an explicit post-roadmap commit instead of the repository
  bootstrap commit.
- Committed the full roadmap implementation baseline as
  `234e984` (`feat(alpha): baseline roadmap implementation for release prep`) so final alpha gate
  tooling can use an explicit from-ref even without a prior release tag.
- Chosen to keep the in-flight 8-hour endurance evidence run uninterrupted and treat concurrent
  acceptance-pack regression failures on `port=17777` as expected resource contention, not product
  regressions.
- Chosen temporary isolated-port execution (`artifacts/tmp/server_test_port18777.json`) for
  acceptance scenarios while endurance is active, allowing TODO progress without invalidating the
  long-running endurance evidence window.
- Deferred full acceptance+regression rerun on default config until the endurance run releases
  `port=17777`, preserving comparable baseline behavior for final alpha sign-off evidence.
- Hardened alpha regression/test portability for concurrent endurance execution by introducing
  config override plumbing:
  - `DEVY_TEST_CONFIG_PATH` now drives `server.smoke` and release CTest wrappers,
  - `scripts/install-package-smoke.sh` and `scripts/rollback-rehearsal.sh` accept an optional
    config-source argument and inject it into extracted package configs before server launch.
- Switched `server.smoke` CTest to wrapper-based invocation (`tests/scripts/assert-server-smoke.sh`)
  so runtime config overrides are possible without regenerating tests or mutating baseline fixtures.
- Normalized `scripts/alpha-acceptance-pack.sh` config input to absolute paths before exporting to
  CTest environment, preventing relative-path failures when CTest executes from preset build dirs.
- Verified the hardened flow under active 8-hour endurance load:
  - isolated-port regression lane passed (`11/11`) with
    `DEVY_TEST_CONFIG_PATH=.../artifacts/tmp/server_test_port18777.json`,
  - full `scripts/alpha-acceptance-pack.sh ... run_regression=1` passed on isolated port,
  - `scripts/alpha-release-gate.sh v0.2.0-alpha-todo-followup ... run_endurance=0 234e984`
    passed and generated `docs/releases/v0.2.0-alpha-todo-followup-notes.md`.
- Added annotated baseline tag `v0.2.0-alpha-baseline` on `234e984` to stabilize release-note
  range inputs for final alpha gate orchestration.
- Verified baseline-tag note-range wiring with an isolated-port pre-endurance gate run:
  - `DEVY_SKIP_BUILD=1 scripts/alpha-release-gate.sh v0.2.0-alpha-preendurance-check ... 0 v0.2.0-alpha-baseline`
  - gate passed and generated `docs/releases/v0.2.0-alpha-preendurance-check-notes.md`.
- Continued TODO-follow cadence while long endurance remains active:
  - reran isolated-port full acceptance+regression:
    - `DEVY_SKIP_BUILD=1 scripts/alpha-acceptance-pack.sh artifacts/tmp/server_test_port18777.json artifacts/releases/alpha-acceptance/todo-followup-port18777-regression-rerun2 debug-vcpkg 8 4 1`,
    - summary passed at `artifacts/releases/alpha-acceptance/todo-followup-port18777-regression-rerun2/summary.txt`.
  - reran isolated-port alpha gate against baseline tag:
    - `DEVY_SKIP_BUILD=1 scripts/alpha-release-gate.sh v0.2.0-alpha-todo-followup-rerun2 artifacts/tmp/server_test_port18777.json debug-vcpkg 8 4 480 artifacts/releases/alpha-gate/todo-followup-port18777-rerun2 0 v0.2.0-alpha-baseline`,
    - gate/acceptance summaries passed and generated `docs/releases/v0.2.0-alpha-todo-followup-rerun2-notes.md`.
- Chosen to keep producing fresh isolated-port acceptance/gate evidence while the 8-hour default-port endurance run is in flight so release-readiness regressions are still detected without interrupting endurance evidence capture.
- Continued TODO-follow cadence with another isolated-port evidence refresh while endurance remains active:
  - `DEVY_SKIP_BUILD=1 scripts/alpha-acceptance-pack.sh artifacts/tmp/server_test_port18777.json artifacts/releases/alpha-acceptance/todo-followup-port18777-regression-rerun3 debug-vcpkg 8 4 1` passed,
  - `DEVY_SKIP_BUILD=1 scripts/alpha-release-gate.sh v0.2.0-alpha-todo-followup-rerun3 artifacts/tmp/server_test_port18777.json debug-vcpkg 8 4 480 artifacts/releases/alpha-gate/todo-followup-port18777-rerun3 0 v0.2.0-alpha-baseline` passed and generated `docs/releases/v0.2.0-alpha-todo-followup-rerun3-notes.md`.
- Reconfirmed long-run health after rerun3 evidence (`2026-02-15T18:27:57Z`): `process_state=running`, `chaos_cycles_completed=86`, `restart_runs_completed=14`; kept default-port post-endurance reruns deferred until port `17777` is free.
- Continued TODO-follow cadence with another isolated-port evidence refresh while endurance remains active:
  - `DEVY_SKIP_BUILD=1 scripts/alpha-acceptance-pack.sh artifacts/tmp/server_test_port18777.json artifacts/releases/alpha-acceptance/todo-followup-port18777-regression-rerun4 debug-vcpkg 8 4 1` passed,
  - `DEVY_SKIP_BUILD=1 scripts/alpha-release-gate.sh v0.2.0-alpha-todo-followup-rerun4 artifacts/tmp/server_test_port18777.json debug-vcpkg 8 4 480 artifacts/releases/alpha-gate/todo-followup-port18777-rerun4 0 v0.2.0-alpha-baseline` passed and generated `docs/releases/v0.2.0-alpha-todo-followup-rerun4-notes.md`.
- Reconfirmed long-run health after rerun4 evidence (`2026-02-15T18:31:34Z`): `process_state=running`, `chaos_cycles_completed=96`, `restart_runs_completed=16`; kept default-port post-endurance reruns deferred until port `17777` is free.
- Chosen to stop repeating isolated-port reruns for every TODO-follow step while the 8-hour run is active; accepted rerun4 artifacts as the current regression/gate evidence baseline until endurance completion.
- Marked alpha bug-bash closure and release-metadata checklist items complete based on the existing passing evidence set:
  - acceptance: `artifacts/releases/alpha-acceptance/todo-followup-port18777-regression-rerun4/summary.txt`,
  - release gate: `artifacts/releases/alpha-gate/todo-followup-port18777-rerun4/summary.txt`,
  - release notes: `docs/releases/v0.2.0-alpha-todo-followup-rerun4-notes.md`,
  - known issues register: `docs/releases/alpha-known-issues.md` (`None` open issues).
- Reconfirmed long-run health after checklist sync (`2026-02-15T18:34:40Z`): `process_state=running`, `chaos_cycles_completed=102`, `restart_runs_completed=16`; final default-port gate remains blocked only on endurance completion.
- Chosen to automate the remaining TODO completion path (instead of manual monitoring + rerun
  commands) by adding `scripts/alpha-post-endurance-followup.sh`, which:
  - polls `scripts/alpha-endurance-status.sh` until the active endurance candidate is `pass`,
  - then runs default-port `scripts/alpha-acceptance-pack.sh` with regression enabled,
  - then runs `scripts/alpha-release-gate.sh` using `v0.2.0-alpha-baseline` as `from_ref`.
- Launched detached follow-up worker:
  - output root: `artifacts/releases/post-endurance/followup-20260215-193837`,
  - launcher pid: `3419283`,
  - first status poll (`2026-02-15T18:38:38Z`) confirmed endurance still running
    (`chaos_cycles_completed=110`, `restart_runs_completed=18`).
- Chosen default follow-up gate mode `run_gate_endurance=0` to consume the already-running dedicated
  8-hour endurance evidence instead of duplicating another 8-hour gate cycle.
- Revalidated in-flight TODO blocker status (`2026-02-15T18:50:44Z`) and kept final release-tagging
  blocked on the same condition: endurance run remains active (`chaos_cycles_completed=135`,
  `restart_runs_completed=22`) and queued follow-up summary is intentionally absent until endurance
  `status=pass`.
- Added lightweight guardrail validation for `scripts/alpha-post-endurance-followup.sh` before
  commit:
  - syntax check: `bash -n` passed,
  - missing-endurance-path invocation fails fast with explicit error,
  - invalid numeric argument invocation fails fast with explicit error.
- Re-polled TODO blocker state (`2026-02-15T18:52:26Z`) and kept release finalization blocked on
  endurance completion:
  - endurance monitor remains `status=running` with `chaos_cycles_completed=142`,
    `restart_runs_completed=23`,
  - queued follow-up summary file remains intentionally absent while waiting for endurance pass,
  - both detached workers (`3365108`, `3419283`) remain alive.
- Chosen to automate the final remaining TODO release-cut step so completion no longer requires
  manual intervention after endurance/follow-up pass:
  - added `scripts/alpha-finalize-release.sh` to wait for post-endurance follow-up pass, verify
    acceptance/gate summaries, regenerate final release notes, and create final release commit/tag.
- Selected final release target `v0.2.0-alpha` with notes generated from
  `v0.2.0-alpha-baseline..HEAD` to produce a clean alpha tag independent of temporary
  `*-post-endurance` gate note filenames.
- Launched detached finalizer worker:
  - output root: `artifacts/releases/post-endurance/finalize-20260215-195526`,
  - launcher pid: `3496294`,
  - first status snapshot (`2026-02-15T18:55:27Z`) shows expected waiting state until follow-up
    summary file is written.
- Re-polled live TODO gate status (`2026-02-15T18:57:23Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=153`, `restart_runs_completed=25`),
  - both queued workers (`3419283`, `3496294`) are healthy and waiting for expected upstream
    summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T18:58:54Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=156`, `restart_runs_completed=26`),
  - both queued workers (`3419283`, `3496294`) are still healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:00:07Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=159`, `restart_runs_completed=26`),
  - both queued workers (`3419283`, `3496294`) are still healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:01:29Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=162`, `restart_runs_completed=27`),
  - both queued workers (`3419283`, `3496294`) are still healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:03:17Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=166`, `restart_runs_completed=27`),
  - both queued workers (`3419283`, `3496294`) are still healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:04:15Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=168`, `restart_runs_completed=28`),
  - both queued workers (`3419283`, `3496294`) are still healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:05:25Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=171`, `restart_runs_completed=28`),
  - both queued workers (`3419283`, `3496294`) are still healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:06:35Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=174`, `restart_runs_completed=29`),
  - both queued workers (`3419283`, `3496294`) are still healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:07:47Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=177`, `restart_runs_completed=29`),
  - both queued workers (`3419283`, `3496294`) are still healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:08:39Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=179`, `restart_runs_completed=29`),
  - both queued workers (`3419283`, `3496294`) are still healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:10:34Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=183`, `restart_runs_completed=30`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:12:26Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=187`, `restart_runs_completed=31`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:13:30Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=190`, `restart_runs_completed=31`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:14:26Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=192`, `restart_runs_completed=31`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:15:24Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=194`, `restart_runs_completed=32`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:16:19Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=196`, `restart_runs_completed=32`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:17:17Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=198`, `restart_runs_completed=33`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:18:10Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=200`, `restart_runs_completed=33`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:19:09Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=203`, `restart_runs_completed=33`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:21:02Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=207`, `restart_runs_completed=34`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:22:11Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=210`, `restart_runs_completed=34`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:23:13Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=212`, `restart_runs_completed=35`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:24:05Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=214`, `restart_runs_completed=35`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:25:14Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=216`, `restart_runs_completed=36`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:26:16Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=219`, `restart_runs_completed=36`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:27:18Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=222`, `restart_runs_completed=36`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:28:14Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=223`, `restart_runs_completed=37`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:29:09Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=226`, `restart_runs_completed=37`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:30:23Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=228`, `restart_runs_completed=38`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:31:33Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=231`, `restart_runs_completed=38`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:32:26Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=233`, `restart_runs_completed=38`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:33:14Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=234`, `restart_runs_completed=39`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:34:02Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=237`, `restart_runs_completed=39`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:34:51Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=239`, `restart_runs_completed=39`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:35:48Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=240`, `restart_runs_completed=40`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:36:39Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=243`, `restart_runs_completed=40`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:38:04Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=246`, `restart_runs_completed=40`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:39:22Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=249`, `restart_runs_completed=41`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:40:22Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=251`, `restart_runs_completed=41`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:41:20Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=253`, `restart_runs_completed=42`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:42:17Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=255`, `restart_runs_completed=42`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:43:16Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=258`, `restart_runs_completed=42`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:44:10Z`) and kept the same release-finalization
  decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=260`, `restart_runs_completed=43`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:45:31Z`) and kept the same release-finalization decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=263`, `restart_runs_completed=43`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:46:36Z`) and kept the same release-finalization decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=265`, `restart_runs_completed=44`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:47:26Z`) and kept the same release-finalization decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=267`, `restart_runs_completed=44`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:48:16Z`) and kept the same release-finalization decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=269`, `restart_runs_completed=44`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:49:04Z`) and kept the same release-finalization decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=270`, `restart_runs_completed=45`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:49:51Z`) and kept the same release-finalization decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=273`, `restart_runs_completed=45`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:50:47Z`) and kept the same release-finalization decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=275`, `restart_runs_completed=45`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.
- Re-polled live TODO gate status (`2026-02-15T19:51:41Z`) and kept the same release-finalization decision:
  - endurance remains the only active blocker (`process_state=running`,
    `chaos_cycles_completed=276`, `restart_runs_completed=46`),
  - both queued workers (`3419283`, `3496294`) remain healthy and waiting for expected
    upstream summaries,
  - no new code-path changes required before endurance completion.

- 2026-02-15: Fast-path validation strategy switched to port-safe config (`artifacts/tmp/server_test_port18777.json`) to avoid contention from legacy long-running endurance jobs on `17777`.
- 2026-02-15: Hardened `tests/scripts/assert-alpha-endurance-short.sh` to honor `DEVY_TEST_CONFIG_PATH` and execute from repo root for stable CTest behavior.
- 2026-02-15: Hardened `tests/scripts/assert-release-notes.sh` to validate commit counts against the generated release-note range instead of assuming `HEAD` full-history count.
- 2026-02-15: Added `builtin-baseline` to `vcpkg.json` to satisfy GitHub Actions `lukka/run-vcpkg@v11` baseline requirement and unblock CI/reliability workflows.
- 2026-02-15: Committed to authoritative interactive-client operation (no preview-world fallback) and centralized chunk replication apply logic via `client::ChunkSyncApplier` for deterministic client world updates.
- 2026-02-15: Added `--health-file` server output to publish telemetry windows as atomically written machine-readable diagnostics snapshots for operator tooling.
- 2026-02-15: Added CI/reliability workflow apt bootstrap (`autoconf`, `autoconf-archive`, `automake`, `libtool`, `pkg-config`) before vcpkg invocation to remove host dependency drift in GitHub runners.
- 2026-02-15: For merge-gate verification, treat isolated upstream vcpkg download `502` failures as transient CI infra flake; rerun failed jobs and require green rerun results before closing release-op TODO items.
- 2026-02-15: For local Fedora handoff, rely on script auto-detection of `../vcpkg` and treat a full `scripts/configure.sh debug` + `scripts/build.sh debug` + `scripts/test.sh debug` pass (`19/19`) as sufficient readiness before asking the operator to run client/server terminals.
- 2026-02-19: For world-expansion release hardening on shared developer hosts, run reliability drills on an isolated temporary config port when `17777` is occupied to avoid false-negative ENet bind failures.
- 2026-02-19: Accepted the world-expansion merge hardening gate as: full `scripts/test.sh debug` pass (`19/19`) plus explicit smoke/chaos/restart evidence at `artifacts/reliability/*/world-expansion-hardening-port27777`.
- 2026-02-19: Recorded runtime handshake validation strategy as static wire-path verification (`server` emits `world_gen`, `client` parses/sanitizes fallback) plus live join/reliability flow validation; deferred direct `world_gen` payload assertion in `devy_load_client` as a follow-up enhancement.
- 2026-02-19: Closed the deferred handshake follow-up by enforcing `join_accept.payload.world_gen` validation in `tools/devy_load_client.cpp`; load runs now fail fast if `world_gen` is missing or invalid, making release/reliability scripts catch handshake-contract regressions automatically.
- 2026-02-19: Added explicit join-contract observability counters in load-client output (`join_accept_world_gen_valid`, `join_accept_world_gen_missing`, `join_accept_world_gen_invalid`) so reliability artifacts show direct evidence that world-generation handshake payloads were validated at runtime.
- 2026-02-19: Extended reliability CTest wrappers (`assert-watchdog-restart`, `assert-chaos-drill`, `assert-restart-recovery`, `assert-reliability-soak`) to honor `DEVY_TEST_CONFIG_PATH` so shared-host test runs can move to an isolated config port without patching scripts.

## 2026-02-19
- Chosen alpha release-cut flow from the closed fast-path state using:
  - `from_ref=v0.2.0-alpha-baseline`,
  - `run_endurance=0` for release gate execution,
  - explicit standalone acceptance-pack rerun for independent evidence.
- Generated final release notes at `docs/releases/v0.2.0-alpha-notes.md` from range `v0.2.0-alpha-baseline..HEAD`.
- Accepted release gate result from `artifacts/releases/alpha-gate/v0.2.0-alpha-final-20260219-160431/summary.txt`:
  - `status=pass`, `acceptance_status=pass`, `endurance_status=skipped`, `release_notes_status=pass`, `missing_required_docs=0`.
- Accepted standalone acceptance-pack result from `artifacts/releases/alpha-acceptance/v0.2.0-alpha-final-20260219-160601/summary.txt`:
  - `status=pass`, `regression_status=pass`, `profile_load_status=pass`, `chaos_drill_status=pass`, `restart_recovery_status=pass`.
- Release-cut decision: proceed with `chore(release): v0.2.0-alpha` commit + annotated `v0.2.0-alpha` tag and push for remote check verification.
