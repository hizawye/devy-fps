 # Devy FPS Delivery Roadmap (12+ Weeks, Task-Level Commits)

  ## Summary

  - Objective: move devy-fps from prototype vertical slice to playable, testable, deployable multiplayer alpha.
  - Strategy: staged delivery with hard acceptance gates, explicit test matrix per milestone, and task-level Conventional Commits.
  - Working mode: every task ends with green local checks + commit; every milestone ends with integration tests + release tag candidate.

  ## Scope Lock

  - In scope: authoritative server sim, state replication, combat loop, block interactions, inventory/loot flow, match lifecycle, observability, CI, packaging, docs.
  - Out of scope (for this plan): cosmetics economy, anti-cheat kernel components, console ports, ranked matchmaking backend.
  - Target: Linux-first build/run, reproducible CMake/vcpkg pipeline.

  ## Public Interfaces / Types Planned

  - shared/net/Protocol.h: expand from JSON packet envelope to versioned schema with message contracts for connect/auth, input, snapshots, events, inventory, match.
  - server runtime API surface: session manager, world sim tick, replication channels, match coordinator modules.
  - shared/game/*: stable data contracts for weapon behavior, damage events, inventory deltas, treasure scoring.
  - Config contracts in config/*.json: version field, validation constraints, fallback defaults.
  - Future compatibility: protocol version handshake + feature flags to support rolling upgrades.

  ## Phase 0: Foundation and Baseline (Week 1)

  1. Milestone M0.1: Repo hygiene + guardrails.
  2. Tasks: add lint/format conventions for C++, add static analysis profile, add deterministic build presets, add baseline scripts.
  3. Tests: clean configure/build on fresh machine, static analysis runs, zero warning budget policy validated.
  4. Commits: one commit per setup task (build:, chore:, ci: types).
  5. Exit criteria: repeatable build from README on clean environment within documented SLA.
  6. Milestone M0.2: Test harness bootstrapping.
  7. Tasks: introduce unit test framework, integration test driver, headless server smoke harness, fixture configs.
  8. Tests: first passing unit test in shared, first server boot smoke test, CI artifact upload test logs.
  9. Commits: one commit per harness unit (test: add shared harness, test: add server smoke).
  10. Exit criteria: CI has mandatory test stage blocking merge.

  ## Phase 1: Networking Core and Protocol Hardening (Weeks 2-4)

  1. Milestone M1.1: Protocol v1 formalization.
  2. Tasks: define message schema table, add protocol version field, input/state/event payload types, serializer/deserializer validation.
  3. Tests: fuzz malformed packet parsing, backward/forward compatibility checks, serialization round-trip suite.
  4. Commits: task-level commits per message family and validation rule.
  5. Exit criteria: protocol docs generated + tests proving stable encode/decode.
  6. Milestone M1.2: Connection/session lifecycle.
  7. Tasks: join request/accept path, player slot allocation, disconnect cleanup, heartbeat + timeout path.
  8. Tests: connect storm test, stale session timeout test, reconnect identity test.
  9. Commits: one commit per session feature.
  10. Exit criteria: 64 simulated clients connect/disconnect without leaks/crashes.
  11. Milestone M1.3: Tick-driven authoritative loop.
  12. Tasks: fixed server tick scheduler, input queueing, deterministic update order, snapshot cadence channel.
  13. Tests: tick drift test over 30m runtime, queue overflow test, snapshot frequency SLA test.
  14. Commits: commit per scheduler/input/snapshot task.
  15. Exit criteria: stable tick jitter within defined threshold.

  ## Phase 2: Movement, World, and Replication (Weeks 5-7)

  1. Milestone M2.1: Authoritative movement.
  2. Tasks: server-side player physics abstraction, client input ack/prediction scaffolding, correction reconciliation.
  3. Tests: latency simulation suite (50/100/200ms), packet loss suite (1/3/5%), correction jitter visual thresholds.
  4. Commits: task-level commits for prediction, reconciliation, and authority checks.
  5. Exit criteria: no positional divergence beyond tolerance window.
  6. Milestone M2.2: World state replication.
  7. Tasks: chunk interest management, relevance culling by distance/frustum proxy, delta snapshot transport.
  8. Tests: bandwidth budget test per client, cold-join world sync test, chunk subscription churn test.
  9. Commits: one commit per replication subsystem.
  10. Exit criteria: stable world sync under max configured players.
  11. Milestone M2.3: Block interaction path.
  12. Tasks: block break/place requests, server validation rules, conflict resolution, replication of block deltas.
  13. Tests: concurrency race test (same block many clients), invalid action rejection tests, persistence-in-match consistency test.
  14. Commits: task-level commits by request/validate/apply/broadcast path.
  15. Exit criteria: consistent block state across clients after stress run.

  ## Phase 3: Combat, Inventory, and Match Rules (Weeks 8-10)

  1. Milestone M3.1: Weapon fire and hit resolution.
  2. Tasks: weapon behavior table integration, ray/projectile authority, damage pipeline, death event broadcast.
  3. Tests: deterministic combat fixture tests, DPS sanity tests per weapon tier, high RTT hit validation tests.
  4. Commits: commit per weapon class + damage subsystem.
  5. Exit criteria: all weapon definitions from config execute as expected in server authority.
  6. Milestone M3.2: Inventory and loot loop.
  7. Tasks: treasure spawn rules, pickup validation, capacity/weight handling, drop-on-death behavior.
  8. Tests: inventory delta correctness tests, duplicate pickup rejection, loot-drop consistency after disconnect/reconnect.
  9. Commits: task-level commits for spawn/pickup/drop/scoring paths.
  10. Exit criteria: end-to-end loot economy loop functional in multiplayer session.
  11. Milestone M3.3: Match lifecycle completion.
  12. Tasks: pre-match, in-match, end-of-match states; timer authority; respawn count enforcement; scoreboard aggregation.
  13. Tests: full match simulation (start->end), timer edge-case tests, respawn exhaustion tests, winner resolution tests.
  14. Commits: one commit per lifecycle state machine increment.
  15. Exit criteria: complete playable match cycle with deterministic outcomes.

  ## Phase 4: Performance, Stability, and Observability (Weeks 11-12)

  1. Milestone M4.1: Profiling and optimization pass.
  2. Tasks: CPU hotspots profiling, memory usage tracking, network payload optimization, chunk mesh/update throttling.
  3. Tests: soak test 2h, peak player load test, regression benchmark against baseline.
  4. Commits: task-level perf commits with benchmark evidence in message/body.
  5. Exit criteria: target FPS/tick/bandwidth budgets met.
  6. Milestone M4.2: Reliability and crash resilience.
  7. Tasks: structured logging improvements, fatal error handling, watchdog restart scripts, config validation on boot.
  8. Tests: chaos scenarios (packet floods, malformed inputs, forced disconnects), restart recovery test.
  9. Commits: one commit per hardening vector.
  10. Exit criteria: no critical crash in 4h soak + actionable logs for all injected failures.
  11. Milestone M4.3: Telemetry and diagnostics.
  12. Tasks: metrics counters (tick lag, packet drop, player count), health endpoints/log summaries, debug overlays optional.
  13. Tests: metrics correctness test, alert-threshold dry run, observability docs verification.
  14. Commits: task-level commits per metric domain.
  15. Exit criteria: operational visibility sufficient for alpha support.

  ## Phase 5: Release Engineering and Delivery (Weeks 13-14+)

  1. Milestone M5.1: CI/CD completion.
  2. Tasks: matrix builds, test shards, artifact packaging, nightly soak pipeline.
  3. Tests: pipeline failure injection test, cache correctness test, reproducible artifact hash checks.
  4. Commits: per pipeline lane/task commit.
  5. Exit criteria: green protected branch pipeline with deterministic artifacts.
  6. Milestone M5.2: Packaging and runtime ops.
  7. Tasks: client/server launch profiles, config templates, release notes generator, rollback strategy docs.
  8. Tests: fresh machine install/run test, upgrade/downgrade protocol test, rollback rehearsal.
  9. Commits: task-level ops/release commits.
  10. Exit criteria: release candidate can be deployed and rolled back safely.
  11. Milestone M5.3: Alpha release gate.
  12. Tasks: bug bash closure, known issues register, final acceptance checklist, tag + release.
  13. Tests: final regression suite, 8h endurance run, multiplayer acceptance scenario pack.
  14. Commits: final fix commits plus chore(release): v0.x.y-alpha.
  15. Exit criteria: signed-off alpha ready for external testers.

  ## Test Program (Always-On Across All Phases)

  - Unit tests: shared logic (protocol, match, inventory, config parsing).
  - Integration tests: server-client handshake, snapshot application, gameplay loops.
  - Load tests: simulated clients with synthetic inputs and packet noise.
  - Soak tests: long-duration stability with memory/tick telemetry.
  - Compatibility tests: protocol version negotiation.
  - Security sanity tests: malformed payload rejection, rate limiting, abuse paths.
  - Regression policy: every bug fix includes failing test first, then fix, then pass.

  ## Commit Policy (Per Task)

  - Format: Conventional Commits.
  - Required pattern: type(scope): concise change + validation note in body.
  - Required task exit: build + relevant tests green before commit.
  - Required milestone exit: merge commit only after milestone checklist passes.
  - Suggested sequence per task:

  1. test(scope): add failing case.
  2. feat/fix(scope): implement change.
  3. refactor(scope): cleanup if needed.
  4. docs(scope): update behavior/contracts.

  ## Delivery Governance

  - Daily: update progress board with task IDs, blockers, next start point.
  - Every 2-3 days: risk review and re-prioritization.
  - Milestone close: demo artifact + metrics + test report.
  - Change control: no scope expansion without explicit tradeoff entry.
  - Definition of done per milestone: code complete + tests complete + docs updated + rollback notes.

  ## Assumptions and Defaults

  - Default protocol transport remains ENet for this roadmap.
  - Default data format remains JSON during v1; binary optimization deferred unless bandwidth gates fail.
  - Default max players remains 64 unless performance tests force adjustment.
  - Default platform priority is Linux desktop/server.
  - Default commit granularity follows your choice: per task.

  ### Unresolved Questions

  - None.
