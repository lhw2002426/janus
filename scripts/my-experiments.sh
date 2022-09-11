#!/usr/bin/env bash

. ./common.bash

CWD=$(pwd)
cd_netre

echo "Generating cache files ... might take a while."
for SCALE in 8-12 16-24 24-32 32-48; do
  ./bin/netre experiments/${SCALE}.ini -a long-term
done

mkdir -p scripts/data/scalability
echo "Running experiments."
for SCALE in 8-12 16-24 24-32 32-48; do
  /usr/bin/time -f "TIME\t${SCALE}\tpug-lookback\t%S\t%U" ./bin/netre experiments/time-${SCALE}.ini -a pug-lookback >res/1core/${SCALE}.txt
done
cd ${CWD}
