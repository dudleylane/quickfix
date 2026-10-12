require './SocketServer'
require 'test/unit'
require_relative 'harness_test_helper'
require "thread"

class SocketServerTestCase < Test::Unit::TestCase

  def test_listen
    port = HarnessTest.port
    socketServer = SocketServer.new
    connectQueue = Queue.new
    receiveQueue = Queue.new
    disconnectQueue = Queue.new

    def socketServer.connectQueue=(q)
      @connectQueue = q
    end
    def socketServer.connectQueue
      return @connectQueue
    end

    def socketServer.receiveQueue=(q)
      @receiveQueue = q
    end
    def socketServer.receiveQueue
      return @receiveQueue
    end

    def socketServer.disconnectQueue=(q)
      @disconnectQueue = q
    end
    def socketServer.disconnectQueue
      return @disconnectQueue
    end

    def socketServer.connectAction(s)
      @connectQueue.push(true)
    end

    def socketServer.disconnectAction(s)
      @disconnectQueue.push(true)
    end

    def socketServer.receiveAction(s)
      while( str = s.gets )
	@receiveQueue.push(str)
      end
    end

    socketServer.connectQueue = connectQueue
    socketServer.receiveQueue = receiveQueue
    socketServer.disconnectQueue = disconnectQueue

    Thread.start do
      socketServer.listen(port)
    end
    socketServer.wait

    s = TCPSocket.open("localhost", port)
    assert(connectQueue.pop)
    s.write("test\r\n")
    s.write("test2\r\n")
    assert_equal("test\r\n", receiveQueue.pop)
    assert_equal("test2\r\n", receiveQueue.pop)
    s.close
    assert(disconnectQueue.pop)
    socketServer.stop()
  end

end
