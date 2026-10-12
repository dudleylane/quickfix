require './Runner'
require 'test/unit'
require_relative 'harness_test_helper'

class RunnerTestCase < Test::Unit::TestCase
  # createProcess waits for something to listen on the port before it reads
  # the definition, so give it a listener.
  def setup
    @port = HarnessTest.port
    @listener = TCPServer.open('127.0.0.1', @port)
  end

  def teardown
    @listener.close
  end

  def test_createProcess
    process = createProcess("foo\nbar\niCONNECT\nbaz", "127.0.0.1", @port)
    assert process.kind_of?(ReflectorClient)
    process = createProcess("iCONNECT\nfoo\nbar\nbaz", "127.0.0.1", @port)
    assert process.kind_of?(ReflectorClient)
    process = createProcess("foo\nbar\neCONNECT\nbaz", "127.0.0.1", @port)
    assert process.kind_of?(ReflectorServer)
    process = createProcess("eCONNECT\nfoo\nbar\nbaz", "127.0.0.1", @port)
    assert process.kind_of?(ReflectorServer)
    process = createProcess("foo\nbar\nbaz", "127.0.0.1", @port)
    assert process.nil?
    process = createProcess("foo\nbar\nbazeCONNECT", "127.0.0.1", @port)
    assert process.nil?
    process = createProcess("", "127.0.0.1", @port)
    assert process.nil?
  end
end
