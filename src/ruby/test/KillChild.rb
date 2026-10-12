# Run by KillTestCase in a process of its own: Thread#kill on the thread running
# a Ruby callback, while it runs it. The kill reached the callback's exception
# handler as a non-exception and the process exited with status 1 (#147); it must
# end the thread, as a kill outside a callback does (#102), and the program must
# carry on. "reactor" kills a SocketAcceptor's own thread, which must also stop
# the acceptor; "dispatcher" kills the thread a ThreadedSocketAcceptor's
# callbacks run on (#104), as Ruby does to every thread at exit.
require 'quickfix_ruby'
$stdout.sync = true

dir, port, kind = ARGV

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
acceptor = (kind == 'dispatcher' ? Quickfix::ThreadedSocketAcceptor : Quickfix::SocketAcceptor).new(acceptorApp, Quickfix::MemoryStoreFactory.new,
	settings(dir, 'acceptor', "ConnectionType=acceptor\nSocketAcceptPort=#{port}\n" \
		"[SESSION]\nBeginString=FIX.4.2\nSenderCompID=ACC\nTargetCompID=INI\n"))
initiator = Quickfix::SocketInitiator.new(App.new, Quickfix::MemoryStoreFactory.new,
	settings(dir, 'initiator', "ConnectionType=initiator\nSocketConnectHost=127.0.0.1\nSocketConnectPort=#{port}\n" \
		"ReconnectInterval=1\nHeartBtInt=30\n[SESSION]\nBeginString=FIX.4.2\nSenderCompID=INI\nTargetCompID=ACC\n"))

entered = Queue.new
# The callback sleeps, so the kill lands inside it.
acceptorApp.inCallback = lambda { entered << true; sleep 5 }
if kind == 'dispatcher'
	acceptor.start
	killedThread = nil
else
	# A thread of the program's own runs block(), so the test can see the kill end
	# it: its ensure runs and nothing after block() does.
	killedThread = Thread.new do
		begin
			acceptor.block
			puts 'afterBlock=true'
		ensure
			puts 'ensured=true'
		end
	end
end
initiatorThread = initiator.start
puts "inCallback=#{!Thread.new { entered.pop }.join(20).nil?}"
killedThread ||= Quickfix._callbackThread
killedThread.kill
puts "killed=#{!killedThread.join(15).nil?}"
puts "status=#{killedThread.status.inspect}"
# The dispatcher's end releases the connection thread waiting for the callback.
acceptor.stop if kind == 'dispatcher'
puts "stopped=#{acceptor.isStopped}"
initiatorThread.kill
initiatorThread.join(15)
puts "ended=true"
