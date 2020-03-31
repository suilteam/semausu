#!/bin/bash
set -e

RUNTIME_DIR=`pwd`/runtime
ROOT_DIR=`pwd`/tests/swept
WAITFOR=`pwd`/wait_for

cd "${ROOT_DIR}"
# Create directories needed by docker-compose containers
mkdir -p ${RUNTIME_DIR}/{postgres,redis,semausu,smtp4dev}
# Append variables used in docker-compose
echo "RUNTIME_DIR=${RUNTIME_DIR}" >> .env
echo "SEMAUSU_VERSION=${CI_COMMIT_SHORT_SHA}" >> .env
# Start docker compose
docker-compose up -d
# Wait for test binary to startup
echo "Waiting for gtytest endpoint to come up"
${WAITFOR} docker:10084 --  echo "gtytest ready to accept tests"
echo "Sweeping gateway"
swept --logdir runtime/gateway/logs --resdir runtme/gateway/results \
  --gtyconf res/gtytest.lua --gtybin gateway --server http://docker