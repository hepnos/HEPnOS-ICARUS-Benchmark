#!/bin/bash

set -e
trap 'kill $(jobs -p) 2> /dev/null' EXIT

rm -f hepnos.ssg dbs.json core

echo "Starting HEPnOS"
bedrock ofi+tcp -c hepnos.json -v info &> bedrock-logs.txt &

echo "Waiting for SSG file"
while [ ! -f hepnos.ssg ]; do sleep 1; done
sleep 1

echo "Querying databases"
hepnos-list-databases ofi+tcp -s hepnos.ssg > dbs.json

echo "Starting benchmark"
../build/hepnos-icarus-benchmark \
    --protocol ofi+tcp \
    --verbose info \
    --product-sizes 128,256,512 \
    --label hepnos \
    --dataset icarus \
    --connection dbs.json

echo "Benchmark completed"
