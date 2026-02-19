#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

config_path="${1:-${repo_root}/config/server_test.json}"
duration_minutes="${2:-15}"
clients="${3:-8}"
out_dir="${4:-${repo_root}/artifacts/reliability/soak/$(date +%Y%m%d-%H%M%S)}"
chaos_seconds="${5:-12}"
restart_phase_seconds="${6:-4}"
restart_every_cycles="${7:-4}"
run_retention_keep="${8:-12}"

if [[ ! -f "${config_path}" && -f "${repo_root}/${config_path}" ]]; then
  config_path="${repo_root}/${config_path}"
fi

if [[ ! -f "${config_path}" ]]; then
  echo "Config file not found: ${config_path}" >&2
  exit 1
fi

utc_now() { date -u +"%Y-%m-%dT%H:%M:%SZ"; }

prune_cycle_dirs() {
  local runs_root="$1"
  local prefix="$2"
  local keep="$3"

  mapfile -t cycle_dirs < <(find "${runs_root}" -mindepth 1 -maxdepth 1 -type d -name "${prefix}-*" | sort -V)
  local total="${#cycle_dirs[@]}"
  if (( total <= keep )); then
    return
  fi

  local remove_count=$((total - keep))
  local i=0
  while (( i < remove_count )); do
    rm -rf "${cycle_dirs[$i]}"
    i=$((i + 1))
  done
}

for pair in \
  "duration_minutes:${duration_minutes}" \
  "clients:${clients}" \
  "chaos_seconds:${chaos_seconds}" \
  "restart_phase_seconds:${restart_phase_seconds}" \
  "restart_every_cycles:${restart_every_cycles}" \
  "run_retention_keep:${run_retention_keep}"; do
  key="${pair%%:*}"
  value="${pair##*:}"
  if ! [[ "${value}" =~ ^[0-9]+$ ]] || (( value <= 0 )); then
    echo "${key} must be a positive integer" >&2
    exit 1
  fi
done

mkdir -p "${out_dir}/runs"
chaos_metrics_csv="${out_dir}/chaos-metrics.csv"
restart_metrics_csv="${out_dir}/restart-metrics.csv"
summary_log="${out_dir}/summary.txt"
timeline_log="${out_dir}/timeline.log"

echo "cycle,family,rate_hz,burst,disconnect_ms,status,invalid_drops,covered_error_categories,forced_disconnects,joined_total,malformed_sent" >"${chaos_metrics_csv}"
echo "cycle,status,phase1_joined,phase2_joined" >"${restart_metrics_csv}"

start_epoch="$(date +%s)"
started_at_utc="$(utc_now)"
deadline_epoch=$((start_epoch + duration_minutes * 60))

watchdog_out="${out_dir}/watchdog"
watchdog_status="fail"
if DEVY_WATCHDOG_KILL_FIRST_AFTER_SECONDS=1 \
  "${repo_root}/scripts/watchdog-server.sh" \
    "${config_path}" \
    3 \
    2 \
    0.2 \
    "${watchdog_out}" \
    6 >>"${timeline_log}" 2>&1; then
  watchdog_status="pass"
fi

cycle=0
chaos_pass=0
chaos_fail=0
restart_pass=0
restart_fail=0

while (( "$(date +%s)" < deadline_epoch )); do
  cycle=$((cycle + 1))
  scenario=$(((cycle - 1) % 6))
  family="mixed"
  rate_hz=30
  burst=3
  disconnect_ms=900
  case "${scenario}" in
    0) family="mixed"; rate_hz=30; burst=3; disconnect_ms=900 ;;
    1) family="unsupported_version"; rate_hz=28; burst=4; disconnect_ms=800 ;;
    2) family="schema"; rate_hz=24; burst=2; disconnect_ms=0 ;;
    3) family="unknown_type"; rate_hz=30; burst=5; disconnect_ms=1200 ;;
    4) family="invalid_json"; rate_hz=35; burst=6; disconnect_ms=600 ;;
    5) family="envelope"; rate_hz=24; burst=2; disconnect_ms=0 ;;
  esac

  chaos_out="${out_dir}/runs/chaos-cycle-${cycle}"
  chaos_status="fail"
  if "${repo_root}/scripts/chaos-drill.sh" \
    "${config_path}" \
    "${clients}" \
    "${chaos_seconds}" \
    "${chaos_out}" \
    "${rate_hz}" \
    "${disconnect_ms}" \
    "${family}" \
    "${burst}" >>"${timeline_log}" 2>&1; then
    chaos_status="pass"
    chaos_pass=$((chaos_pass + 1))
  else
    chaos_fail=$((chaos_fail + 1))
  fi

  chaos_summary="${chaos_out}/summary.txt"
  invalid_drops=0
  covered_categories=0
  forced_disconnects=0
  joined_total=0
  malformed_sent=0
  if [[ -f "${chaos_summary}" ]]; then
    invalid_drops="$(grep -E '^invalid_packet_drops=' "${chaos_summary}" | tail -n1 | cut -d= -f2 || true)"
    covered_categories="$(grep -E '^covered_error_categories=' "${chaos_summary}" | tail -n1 | cut -d= -f2 || true)"
    forced_disconnects="$(grep -E '^forced_disconnects=' "${chaos_summary}" | tail -n1 | cut -d= -f2 || true)"
    joined_total="$(grep -E '^joined_total=' "${chaos_summary}" | tail -n1 | cut -d= -f2 || true)"
    malformed_sent="$(grep -E '^malformed_sent=' "${chaos_summary}" | tail -n1 | cut -d= -f2 || true)"
  fi
  echo "${cycle},${family},${rate_hz},${burst},${disconnect_ms},${chaos_status},${invalid_drops:-0},${covered_categories:-0},${forced_disconnects:-0},${joined_total:-0},${malformed_sent:-0}" >>"${chaos_metrics_csv}"
  prune_cycle_dirs "${out_dir}/runs" "chaos-cycle" "${run_retention_keep}"

  if (( cycle % restart_every_cycles == 0 )) && (( "$(date +%s)" < deadline_epoch )); then
    restart_out="${out_dir}/runs/restart-cycle-${cycle}"
    restart_status="fail"
    if "${repo_root}/scripts/restart-recovery.sh" \
      "${config_path}" \
      "${clients}" \
      "${restart_phase_seconds}" \
      "${restart_out}" >>"${timeline_log}" 2>&1; then
      restart_status="pass"
      restart_pass=$((restart_pass + 1))
    else
      restart_fail=$((restart_fail + 1))
    fi

    restart_summary="${restart_out}/summary.txt"
    phase1_joined=0
    phase2_joined=0
    if [[ -f "${restart_summary}" ]]; then
      phase1_joined="$(grep -E '^phase1_joined=' "${restart_summary}" | tail -n1 | cut -d= -f2 || true)"
      phase2_joined="$(grep -E '^phase2_joined=' "${restart_summary}" | tail -n1 | cut -d= -f2 || true)"
    fi
    echo "${cycle},${restart_status},${phase1_joined:-0},${phase2_joined:-0}" >>"${restart_metrics_csv}"
    prune_cycle_dirs "${out_dir}/runs" "restart-cycle" "${run_retention_keep}"
  fi
done

end_epoch="$(date +%s)"
finished_at_utc="$(utc_now)"
elapsed_seconds=$((end_epoch - start_epoch))

chaos_count=$((chaos_pass + chaos_fail))
restart_count=$((restart_pass + restart_fail))
overall_status="pass"
if [[ "${watchdog_status}" != "pass" ]] || (( chaos_fail > 0 )) || (( chaos_count == 0 )) || (( restart_fail > 0 )); then
  overall_status="fail"
fi

retained_chaos_dirs="$(find "${out_dir}/runs" -mindepth 1 -maxdepth 1 -type d -name 'chaos-cycle-*' | wc -l | tr -d ' ')"
retained_restart_dirs="$(find "${out_dir}/runs" -mindepth 1 -maxdepth 1 -type d -name 'restart-cycle-*' | wc -l | tr -d ' ')"

{
  echo "schema_version=1"
  echo "summary_kind=reliability_soak"
  echo "started_at_utc=${started_at_utc}"
  echo "finished_at_utc=${finished_at_utc}"
  echo "elapsed_seconds=${elapsed_seconds}"
  echo "status=${overall_status}"
  echo "config=${config_path}"
  echo "out_dir=${out_dir}"
  echo "retention_policy=cycle_runs:max=${run_retention_keep}"
  echo "run_retention_keep=${run_retention_keep}"
  echo "retained_chaos_dirs=${retained_chaos_dirs}"
  echo "retained_restart_dirs=${retained_restart_dirs}"
  echo "chaos_metrics_csv=$(basename "${chaos_metrics_csv}")"
  echo "restart_metrics_csv=$(basename "${restart_metrics_csv}")"
  echo "timeline_log=$(basename "${timeline_log}")"
  echo "duration_minutes=${duration_minutes}"
  echo "clients=${clients}"
  echo "chaos_seconds=${chaos_seconds}"
  echo "restart_phase_seconds=${restart_phase_seconds}"
  echo "restart_every_cycles=${restart_every_cycles}"
  echo "watchdog_status=${watchdog_status}"
  echo "chaos_cycles=${chaos_count}"
  echo "chaos_pass=${chaos_pass}"
  echo "chaos_fail=${chaos_fail}"
  echo "restart_runs=${restart_count}"
  echo "restart_pass=${restart_pass}"
  echo "restart_fail=${restart_fail}"
  echo
  echo "chaos_trends:"
  awk -F',' '
    NR == 1 { next }
    {
      count += 1
      invalid += $7
      covered += $8
      forced += $9
      joined += $10
      malformed += $11
      if (count == 1 || $7 < min_invalid) min_invalid = $7
      if (count == 1 || $7 > max_invalid) max_invalid = $7
      if (count == 1 || $8 < min_covered) min_covered = $8
      if (count == 1 || $8 > max_covered) max_covered = $8
      if (count == 1 || $9 < min_forced) min_forced = $9
      if (count == 1 || $9 > max_forced) max_forced = $9
      if (count == 1 || $10 < min_joined) min_joined = $10
      if (count == 1 || $10 > max_joined) max_joined = $10
      if (count == 1 || $11 < min_malformed) min_malformed = $11
      if (count == 1 || $11 > max_malformed) max_malformed = $11
    }
    END {
      if (count == 0) {
        print "  no chaos cycles"
        exit
      }
      printf("  invalid_packet_drops avg=%.2f min=%d max=%d\n", invalid / count, min_invalid, max_invalid)
      printf("  covered_error_categories avg=%.2f min=%d max=%d\n", covered / count, min_covered, max_covered)
      printf("  forced_disconnects avg=%.2f min=%d max=%d\n", forced / count, min_forced, max_forced)
      printf("  joined_total avg=%.2f min=%d max=%d\n", joined / count, min_joined, max_joined)
      printf("  malformed_sent avg=%.2f min=%d max=%d\n", malformed / count, min_malformed, max_malformed)
    }
  ' "${chaos_metrics_csv}"
  echo
  echo "recent_cycles:"
  tail -n 5 "${chaos_metrics_csv}"
} >"${summary_log}"

if [[ "${overall_status}" == "pass" ]]; then
  echo "Reliability soak succeeded. Artifacts: ${out_dir}"
  exit 0
fi

echo "Reliability soak failed. See ${summary_log}" >&2
exit 1
