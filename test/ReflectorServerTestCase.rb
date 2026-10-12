require './ReflectorServer'
require './FixParser'
require 'test/unit'
require_relative 'harness_test_helper'
require 'socket'
require 'thread'

# The harness's server mode: on eCONNECT it listens, then it expects what E
# lines say and sends what I lines say. This was written for an older
# ReflectorServer driven through SocketServer#listen and R lines, which no
# longer exist, and could not load (#135).
class ReflectorServerTestCase < Test::Unit::TestCase

  def test_reflectMessages
    port = HarnessTest.port
    # fixify! fills in BodyLength and CheckSum, as the harness does for a definition.
    logon = Reflector.new.fixify!("8=FIX.4.2\00135=A\001108=30\001".b)
    reply = Reflector.new.fixify!("8=FIX.4.2\00135=A\001108=10\001".b)
    messages = "eCONNECT\nE#{logon}\nI#{reply}\neDISCONNECT\n"

    server = ReflectorServer.new(messages, "localhost", port)
    compared = Queue.new
    server.define_singleton_method(:compareAction) { |e, a| compared.push([e, a]) }
    serving = Thread.new { server.start }

    client = nil
    50.times do
      begin
        client = TCPSocket.open("localhost", port)
        break
      rescue Errno::ECONNREFUSED
        sleep 0.1
      end
    end
    assert_not_nil(client)
    client.write(logon)
    assert_equal(reply, FixParser.new(client).readFixMessage)
    client.close
    assert_not_nil(serving.join(10))
    assert_equal([logon, logon], compared.pop)
  end
end
