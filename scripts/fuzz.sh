#!/usr/bin/env bash
# Build the libFuzzer targets in fuzz/ with Clang, AddressSanitizer and
# UBSan, and run each one for a share of a fixed time budget.
#
#   bash scripts/fuzz.sh [SECONDS]      (default 60, split across the targets)
#
# The working corpus starts from fuzz/corpus/ plus, if scripts/fetch_logs.sh
# has run, real lines from the gpsd logs. It lives in build-fuzz/ and grows
# between runs. Any crashing input is saved under build-fuzz/artifacts/ and
# the script exits non-zero; copy it into fuzz/corpus/<target>/ to keep it as
# a regression test (ctest runs the corpus in every build).
set -euo pipefail
export LC_ALL=C  # byte ranges in the regexes below, and a stable sort

total="${1:-60}"
if ! [[ "${total}" =~ ^[0-9]+$ ]] || (( total < 2 )); then
  echo "usage: $0 [SECONDS]" >&2
  exit 2
fi

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${root}/build-fuzz"
targets=(parser decode)
per=$(( total / ${#targets[@]} ))
CXX="${CXX:-clang++}"

if ! command -v "${CXX}" >/dev/null; then
  echo "error: ${CXX} not found (sudo apt-get install clang libclang-rt-dev)" >&2
  exit 1
fi

cmake -S "${root}" -B "${build}" -DCMAKE_CXX_COMPILER="${CXX}" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DNMEA_GUARD_BUILD_FUZZERS=ON -DNMEA_GUARD_BUILD_TESTS=OFF -DNMEA_GUARD_BUILD_TOOLS=OFF >/dev/null
cmake --build "${build}" --parallel 2 >/dev/null

work="$(mktemp -d "${TMPDIR:-/tmp}/nmea-guard-fuzz.XXXXXX")"
pid=""
cleanup() {
  [[ -n "${pid}" ]] && kill "${pid}" 2>/dev/null || true
  rm -rf "${work}"
}
trap cleanup EXIT
trap 'exit 130' INT TERM

# Seeds from the real logs: whole short stretches for the parser, single
# sentence bodies (no '$' or checksum) for the decoder.
logs=("${root}"/data/gpsd/*.log)
if [[ -e "${logs[0]}" ]]; then
  mkdir -p "${work}/parser" "${work}/decode"
  for log in "${logs[@]}"; do
    head -c 2048 "${log}" > "${work}/parser/$(basename "${log}")"
  done
  # One file per body, no trailing newline. (awk rather than head, which
  # would end the pipe early and trip pipefail.)
  grep -haE '^\$[A-Z]{5},[ -~]*\*[0-9A-F]{2}' "${logs[@]}" | tr -d '\r' \
    | sed -E 's/^\$//; s/\*[0-9A-F]{2}$//' | sort -u \
    | awk -v dir="${work}/decode" 'NR <= 2000 { f = dir "/b" NR; printf "%s", $0 > f; close(f) }'
fi

mkdir -p "${build}/artifacts"
status=0
for t in "${targets[@]}"; do
  corpus="${build}/corpus/${t}"
  mkdir -p "${corpus}"
  seeds=("${root}/fuzz/corpus/${t}")
  [[ -d "${work}/${t}" ]] && seeds+=("${work}/${t}")
  echo "fuzz_${t}: ${per}s with ASan+UBSan"
  # The first corpus directory receives new inputs; the others are read only.
  "${build}/fuzz/fuzz_${t}" "${corpus}" "${seeds[@]}" \
    -max_total_time="${per}" -timeout=10 -rss_limit_mb=1024 -max_len=4096 \
    -artifact_prefix="${build}/artifacts/${t}-" -print_final_stats=1 \
    > "${build}/fuzz_${t}.log" 2>&1 &
  pid=$!
  if wait "${pid}"; then
    runs="$(grep -oE 'stat::number_of_executed_units: *[0-9]+' "${build}/fuzz_${t}.log" | grep -oE '[0-9]+$' || echo '?')"
    cov="$(grep -oE 'cov: [0-9]+' "${build}/fuzz_${t}.log" | tail -n 1 | grep -oE '[0-9]+' || echo '?')"
    echo "  ok: ${runs} inputs, ${cov} coverage points, corpus $(ls "${corpus}" | wc -l) files"
  else
    status=1
    echo "  FAILED; last lines of ${build#"${root}"/}/fuzz_${t}.log:" >&2
    tail -n 40 "${build}/fuzz_${t}.log" >&2
  fi
  pid=""
done
exit "${status}"
