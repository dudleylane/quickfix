#!/bin/bash

set -e

# Ruby 3 unbundled rexml, which Processor.rb requires.  Fail with something
# actionable rather than a bare LoadError from deep inside the generator.
if ! ruby -e "require 'rexml/document'" >/dev/null 2>&1; then
  echo "generate.sh: the 'rexml' gem is required (gem install --user-install rexml)" >&2
  exit 1
fi

./generate_c++.sh
ruby -I. Generator.rb

# The generators emit unformatted C++; the tree is clang-format clean and CI
# enforces it tree-wide (.github/workflows/format.yml), so format what was just
# written.  Without this step a regeneration shows ~18k lines of pure
# whitespace churn and fails the format job.
FMT=${CLANG_FORMAT:-clang-format-22}
if ! command -v "$FMT" >/dev/null 2>&1; then
  echo "generate.sh: $FMT not found; set CLANG_FORMAT to your clang-format" >&2
  exit 1
fi
# Only the top-level Fix*.h: src/C++/.clang-format-ignore exempts the per-version
# directories, so clang-format -i leaves them as generated (#134).
"$FMT" -i ../src/C++/Fix*.h
