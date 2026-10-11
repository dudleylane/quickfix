#!/bin/sh
# Installs the Ruby binding where Ruby looks for it: quickfix.so, which make_ruby.sh builds in this
# directory, into RbConfig's sitearchdir, and the quickfix*.rb files beside it into sitelibdir.
# Run make_ruby.sh first. Honours DESTDIR for staging. Works from any directory.
#
# Ruby's site directories come from RbConfig, not from a prefix: Fedora puts them under /usr/local
# while Ruby's own prefix is /usr. There was no install for the Ruby binding before #108.
set -eu
cd "$(dirname "$0")"

[ -f quickfix.so ] || { echo "install.sh: no quickfix.so here -- run make_ruby.sh first" >&2; exit 1; }

arch=$(ruby -rrbconfig -e 'print RbConfig::CONFIG["sitearchdir"]')
lib=$(ruby -rrbconfig -e 'print RbConfig::CONFIG["sitelibdir"]')
dest=${DESTDIR:-}

install -d "$dest$arch" "$dest$lib"
install -m 755 quickfix.so "$dest$arch/"
install -m 644 quickfix*.rb "$dest$lib/"
echo "installed quickfix.so in $dest$arch and the Ruby files in $dest$lib"
