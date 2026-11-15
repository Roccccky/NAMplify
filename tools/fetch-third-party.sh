#!/usr/bin/env bash
# Fetches the third-party sources NAMplify builds against, at the exact
# versions it is tested with. Used by CI and by anyone building from source.
#
#   tools/fetch-third-party.sh
#
# third_party/ is not part of the repository. If a folder is already there it
# is left alone, so an offline checkout keeps working.
#
# NeuralAmpModelerCore is pinned to a commit, not a tag: its version header
# says 0.5.5, but no tag of that name exists. A clone of `main` would be a
# different engine tomorrow.

set -euo pipefail
cd "$(dirname "$0")/.."

JUCE_TAG=9.0.1
EIGEN_TAG=3.4.0
NAM_COMMIT=2563c0fd4cb1f9ce457d89a761738ea15097e1f3

mkdir -p third_party

if [ ! -d third_party/JUCE ]; then
  git clone -q --depth 1 --branch "$JUCE_TAG" https://github.com/juce-framework/JUCE.git third_party/JUCE
fi

if [ ! -d third_party/eigen ]; then
  git clone -q --depth 1 --branch "$EIGEN_TAG" https://gitlab.com/libeigen/eigen.git third_party/eigen
fi

if [ ! -d third_party/NeuralAmpModelerCore ]; then
  git init -q third_party/NeuralAmpModelerCore
  git -C third_party/NeuralAmpModelerCore remote add origin https://github.com/sdatkinson/NeuralAmpModelerCore.git
  git -C third_party/NeuralAmpModelerCore fetch -q --depth 1 origin "$NAM_COMMIT"
  git -C third_party/NeuralAmpModelerCore checkout -q FETCH_HEAD
fi

# Refuse to build against something else than what the tests were written for.
have="$(git -C third_party/NeuralAmpModelerCore rev-parse HEAD 2>/dev/null || echo unknown)"
if [ "$have" != "$NAM_COMMIT" ] && [ -d third_party/NeuralAmpModelerCore/.git ]; then
  echo "third_party/NeuralAmpModelerCore is at $have, expected $NAM_COMMIT" >&2
  exit 1
fi

echo "third_party: JUCE $JUCE_TAG · Eigen $EIGEN_TAG · NeuralAmpModelerCore ${NAM_COMMIT:0:8}"
