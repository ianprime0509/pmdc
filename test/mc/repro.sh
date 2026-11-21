#!/bin/bash
# Inspired by https://github.com/OPNA2608/projmd
# Creates/updates MC expected output files.
# Usage: ./repro.sh ../../original/mc48s cases cases/INPUT.MML
set -euo pipefail

die() { echo "repro: $@" >&2; exit 1; }

mcdir=${1:?missing MC files directory}
shift
casesdir=${1:?missing test cases directory}
shift
inputfile=${1:?missing input file}
shift

filestem=$(basename --suffix .MML "$inputfile")
[[ -f "$mcdir/MC.EXE" ]] || die "MC.EXE not found in $mcdir"

workdir=$(mktemp -d)
cleanup() { [[ -n "${workdir:-}" && -d "$workdir" ]] && rm -rf -- "$workdir"; }
trap cleanup EXIT

cp -- "$mcdir/MC.EXE" "$workdir/"
datadir=$workdir/data
mkdir "$datadir"
cp -- "$inputfile" "$datadir"

configfile=$workdir/dosbox-x.conf
cat >"$configfile" <<EOF
[autoexec]
MOUNT C $workdir
C:
CD data
C:\MC.EXE $filestem.MML >$filestem.OUT
EXIT
EOF
SDL_AUDIODRIVER=dummy xvfb-run dosbox-x -nomenu -silent -conf "$configfile" >&2

cp -f -- "$datadir"/* "$casesdir/"
tr -d '\r' <"$datadir/$filestem.OUT" | iconv -f cp932 -t utf8 >"$casesdir/$filestem.OUT"
