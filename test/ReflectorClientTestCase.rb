require './ReflectorClient'
require './ReflectorServer'
require 'test/unit'
require_relative 'harness_test_helper'
require 'socket'
require 'thread'

# The harness's two modes against each other: a ReflectorServer with one
# definition and a ReflectorClient with its mirror image, so each side's E
# lines are the other side's I lines. Every expectation must be compared, and
# compared equal. Written for an older ReflectorServer, it could not load (#135).
class ReflectorClientTestCase < Test::Unit::TestCase

  def test_reflectMessages
    port = HarnessTest.port
    message = lambda { |body| Reflector.new.fixify!("8=FIX.4.2\001#{body}\001".b) }
    logon, reply = message.("35=A\001108=30"), message.("35=A\001108=10")
    logout, logoutReply = message.("35=5\00158=bye"), message.("35=5")

    server = ReflectorServer.new("eCONNECT\nE#{logon}\nI#{reply}\nE#{logout}\nI#{logoutReply}\neDISCONNECT\n",
                                 "localhost", port)
    client = ReflectorClient.new("iCONNECT\nI#{logon}\nE#{reply}\nI#{logout}\nE#{logoutReply}\niDISCONNECT\n",
                                 "localhost", port)
    compared = Queue.new
    [server, client].each do |side|
      side.define_singleton_method(:compareAction) { |e, a| compared.push([e, a]) }
      # Runner.rb's extendProcess gives a process this; a test must too.
      side.define_singleton_method(:errorAction) { |lineNum, line| raise "line #{lineNum}: #{$!}" }
    end

    serving = Thread.new { server.start }
    # A probe connection would take the server's one accept, so wait for its
    # listener by failing to bind the port.
    50.times do
      begin
        TCPServer.open('localhost', port).close
        sleep 0.1
      rescue Errno::EADDRINUSE
        break
      end
    end

    client.start
    client.stop
    assert_not_nil(serving.join(10))

    results = []
    results << compared.pop until compared.empty?
    assert_equal(4, results.length)
    results.each { |expected, actual| assert_equal(expected, actual) }
  end
end
