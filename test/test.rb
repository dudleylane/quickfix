#!/usr/bin/env ruby
# Runs the acceptance harness's own tests -- the Comparator that judges every
# definition, the parser, reflector and socket server around it -- with
# test-unit. Run from test/: ruby -I. test.rb. They were written for runit,
# which Ruby removed in 1.9, and could not load until #135.
require 'test/unit'
require_relative 'harness_test_helper'
require_relative 'ComparatorTestCase'
require_relative 'FixParserTestCase'
require_relative 'ReflectorTestCase'
require_relative 'ReflectorClientTestCase'
require_relative 'ReflectorServerTestCase'
require_relative 'RunnerTestCase'
require_relative 'SocketServerTestCase'
