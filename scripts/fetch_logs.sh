#!/usr/bin/env bash
# Download gpsd's receiver regression logs (test/daemon/*.log) into data/gpsd/.
#
# The logs are real captures submitted by gpsd users for dozens of receivers.
# They are BSD-licensed by the GPSD project and are not committed here.
# Pinned to a release tag so test results are reproducible.
set -euo pipefail

GPSD_REF="${GPSD_REF:-release-3.25}"
URL="https://gitlab.com/gpsd/gpsd/-/archive/${GPSD_REF}/gpsd-${GPSD_REF}.tar.gz?path=test/daemon"

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dest="${root}/data/gpsd"

if [[ -f "${dest}/.ref" && "$(cat "${dest}/.ref")" == "${GPSD_REF}" ]] && compgen -G "${dest}/*.log" >/dev/null; then
  echo "gpsd logs (${GPSD_REF}) already in ${dest#"${root}"/}: $(ls "${dest}"/*.log | wc -l) files"
  exit 0
fi

work="$(mktemp -d "${TMPDIR:-/tmp}/nmea-guard-fetch.XXXXXX")"
trap 'rm -rf "${work}"' EXIT

echo "downloading gpsd ${GPSD_REF} test/daemon ..."
curl --fail --silent --show-error --location --retry 3 -o "${work}/logs.tar.gz" "${URL}"
tar -xzf "${work}/logs.tar.gz" -C "${work}"

src="$(find "${work}" -type d -path '*/test/daemon' | head -n 1)"
if [[ -z "${src}" ]]; then
  echo "error: archive has no test/daemon directory" >&2
  exit 1
fi

rm -rf "${dest}"
mkdir -p "${dest}"
# Only the captures; the .chk files are gpsd's expected JSON output.
find "${src}" -maxdepth 1 -type f -name '*.log' -exec cp {} "${dest}/" \;
echo "${GPSD_REF}" > "${dest}/.ref"

count="$(ls "${dest}"/*.log | wc -l)"
nmea="$(grep -lE '^\$G[A-Z](GGA|RMC|GSA|VTG),' "${dest}"/*.log | wc -l)"
echo "fetched ${count} logs into data/gpsd/ (${nmea} contain GGA/RMC/GSA/VTG sentences)"
