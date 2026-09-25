# NOTE: this block must run BEFORE require 'mkmf'. mkmf snapshots these flags
# into its own globals as it loads, so rewriting RbConfig afterwards has no
# effect on the commands it builds -- verified: the annobin spec still appeared
# in mkmf.log's failing command line.
#
# mkmf compiles its probes with the flags Ruby itself was built with. On
# RHEL/CentOS those carry -specs=/usr/lib/rpm/redhat/redhat-annobin-cc1, which
# loads the gcc-annobin plugin. That plugin is built against the system GCC, and
# GCC plugins are tied to the compiler's plugin ABI, so it cannot load into
# gcc-toolset-15 -- which this tree requires, because FieldMap.h uses
# <flat_map> and GCC 14 does not have it. Without this, the very first probe
# dies with "inaccessible plugin file plugin/gcc-annobin.so" and mkmf reports
# the misleading "You have to install development tools first".
#
# Only the annobin spec is removed. The other three RPM hardening specs
# (redhat-hardened-cc1, redhat-hardened-ld, redhat-hardened-ld-errors) were
# each checked against gcc-toolset-15 and work, so the distro's hardening is
# kept; annobin only emits metadata for security auditing.
#
# This must run before have_library below: that is where mkmf first compiles,
# and it precedes the CONFIG rewriting further down.
require 'rbconfig'
%w[CFLAGS CXXFLAGS LDFLAGS DLDFLAGS].each do |key|
  [RbConfig::CONFIG, RbConfig::MAKEFILE_CONFIG].each do |config|
    config[key] = config[key].gsub(/-specs=\S*annobin\S*/, '') if config[key]
  end
end

require 'mkmf'

dir_config("quickfix", ["../..", "../../include", "../C++", "../swig"], "../../lib")
have_library("quickfix")

CONFIG["warnflags"].sub!('-Wdeprecated-declarations', '-Wno-deprecated-declarations -Wno-deprecated')
CONFIG["warnflags"].sub!('-Wall', '')
CONFIG["warnflags"].sub!('-Wextra', '')

if( ENV['CXX'] != nil )
  CONFIG["LDSHARED"].gsub!("gcc", ENV['CXX']) 
  CONFIG["LDSHARED"].gsub!("cc", ENV['CXX'])
end

RbConfig::MAKEFILE_CONFIG['CXX'] = ENV['CXX'] if ENV['CXX']

additional_flags = ' -Wno-deprecated-declarations -Wno-deprecated -Wno-uninitialized -Wno-unused-but-set-variable -Wno-inconsistent-missing-override -Wno-register'
$CFLAGS << additional_flags
$CXXFLAGS << additional_flags

# make_ruby.sh exports CXX, CXXFLAGS and LIBS, but only CXX was ever read --
# the other two were silently discarded. That is why the module was compiled
# without -std=c++23 and died on FieldMap.h's std::flat_map even once the
# annobin spec was out of the way.
#
# C++23 is not optional for this tree, so it is supplied when the caller does
# not pick a standard, rather than only when CXXFLAGS happens to be set.
env_cxxflags = ENV['CXXFLAGS'].to_s
env_cxxflags += ' -std=c++23' unless env_cxxflags =~ /-std=/
$CXXFLAGS << ' ' << env_cxxflags

$libs << ' ' << ENV['LIBS'] unless ENV['LIBS'].to_s.empty?

create_makefile("quickfix")
