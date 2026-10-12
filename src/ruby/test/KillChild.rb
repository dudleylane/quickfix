# Run by KillTestCase in a process of its own: Thread#kill on an acceptor's
# thread while that thread is inside a Ruby callback. The kill reached the
# callback's exception handler as a non-exception and the process exited with
# status 1 (#147); it must stop the transport, as a kill outside a callback does
# (#102), and the program must carry on.
require 'quickfix_ruby'
$stdout.sync = true

dir, port = ARGV

class App < Quickfix::Application
	attr_accessor :inCallback
	def onCreate(sessionID); end
	def onLogon(sessionID); @inCallback&.call; end
	def onLogout(sessionID); end
	def toAdmin(message, sessionID); end
	def fromAdmin(message, sessionID); end
	def toApp(message, sessionID); end
	def fromApp(message, sessionID); end
end

def settings(dir, name, body)
	File.write("#{dir}/#{name}.cfg", "[DEFAULT]\nStartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\n" + body)
	Quickfix::SessionSettings.new("#{dir}/#{name}.cfg")
end

acceptorApp = App.new
acceptor = Quickfix::SocketAcceptor.new(acceptorApp, Quickfix::MemoryStoreFactory.new,
	settings(dir, 'acceptor', "ConnectionType=acceptor\nSocketAcceptPort=#{port}\n" \
		"[SESSION]\nBeginString=FIX.4.2\nSenderCompID=ACC\nTargetCompID=INI\n"))
initiator = Quickfix::SocketInitiator.new(App.new, Quickfix::MemoryStoreFactory.new,
	settings(dir, 'initiator', "ConnectionType=initiator\nSocketConnectHost=127.0.0.1\nSocketConnectPort=#{port}\n" \
		"ReconnectInterval=1\nHeartBtInt=30\n[SESSION]\nBeginString=FIX.4.2\nSenderCompID=INI\nTargetCompID=ACC\n"))

entered = Queue.new
# The callback runs on the acceptor's own thread and sleeps, so the kill lands inside it.
acceptorApp.inCallback = lambda { entered << true; sleep 5 }
# A thread of the program's own runs block(), so the test can see the kill end it:
# its ensure runs and nothing after block() does.
acceptorThread = Thread.new do
	begin
		acceptor.block
		puts 'afterBlock=true'
	ensure
		puts 'ensured=true'
	end
end
initiatorThread = initiator.start
puts "inCallback=#{!Thread.new { entered.pop }.join(20).nil?}"
acceptorThread.kill
puts "killed=#{!acceptorThread.join(15).nil?}"
puts "stopped=#{acceptor.isStopped}"
puts "status=#{acceptorThread.status.inspect}"
initiatorThread.kill
initiatorThread.join(15)
puts "ended=true"
