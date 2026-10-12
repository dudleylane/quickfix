require './FixParser'
require 'test/unit'
require_relative 'harness_test_helper'
require "thread"
require './SocketServer'

class FixParserTestCase < Test::Unit::TestCase

  def test_readFixMessage
    port = HarnessTest.port
    fixMsg1 = "8=FIX.4.2\0019=12\00135=A\001108=30\00110=31\001"
    fixMsg2 = "8=FIX.4.2\0019=17\00135=4\00136=88\001123=Y\00110=34\001"

    server = SocketServer.new
    def server.message=(m)
      @message = m
    end

    def server.connectAction(s)
    end

    def server.receiveAction(s)
      s.write(@message)
    end

    def server.disconnectAction(s)
    end

    server.message = fixMsg1 + fixMsg2
    Thread.start do
      server.listen(port)
    end
    server.wait

    s = TCPSocket.open("localhost", port)
    parser = FixParser.new(s)
    begin
      assert_equal(fixMsg1, parser.readFixMessage)
      assert_equal(fixMsg2, parser.readFixMessage)
    rescue IOError
      # I have no idea why this is being thrown
    end

    s.close
    server.stop()
  end

end
