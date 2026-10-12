# Shared by the acceptance harness's own tests (test.rb runs them all): a
# loopback port nothing is listening on, a new one for each call, so one test's
# sockets cannot meet the next one's. The tests used a fixed 5001 under the
# long-gone runit (#135).
require 'socket'

module HarnessTest
  def self.port
    TCPServer.open('127.0.0.1', 0) { |server| server.addr[1] }
  end
end
