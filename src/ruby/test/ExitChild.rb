# Run by ExitTestCase in a process of its own: an acceptor and an initiator,
# started and logged on to each other, and one never started, all left running
# when the script ends. Ruby's exit used to free them after their factories and
# crash in their destructors (#105).
require 'quickfix_ruby'
$stdout.sync = true

dir, port, kind = ARGV
# The threaded transports call back on threads Ruby did not create (#104).
acceptorClass, initiatorClass = kind == 'threaded' ?
	[Quickfix::ThreadedSocketAcceptorBase, Quickfix::ThreadedSocketInitiatorBase] :
	[Quickfix::SocketAcceptor, Quickfix::SocketInitiator]

class App < Quickfix::Application
	def onCreate(sessionID); end
	def onLogon(sessionID); end
	def onLogout(sessionID); end
	def toAdmin(message, sessionID); end
	def fromAdmin(message, sessionID); end
	def toApp(message, sessionID); end
	def fromApp(message, sessionID); end
end

class QuietLog < Quickfix::Log
	def clear; end
	def backup; end
	def onIncoming(s); end
	def onOutgoing(s); end
	def onEvent(s); end
end

class QuietLogFactory < Quickfix::LogFactory
	def initialize; super; @logs = []; end
	def create(*args); log = QuietLog.new; @logs << log; log; end
	def destroy(log); @logs.delete(log); end
end

def settings(dir, name, body)
	File.write("#{dir}/#{name}.cfg", "[DEFAULT]\nStartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\n" + body)
	Quickfix::SessionSettings.new("#{dir}/#{name}.cfg")
end

acceptor = acceptorClass.new(App.new, Quickfix::MemoryStoreFactory.new,
	settings(dir, 'acceptor', "ConnectionType=acceptor\nSocketAcceptPort=#{port}\n" \
		"[SESSION]\nBeginString=FIX.4.2\nSenderCompID=ACC\nTargetCompID=INI\n"), QuietLogFactory.new)
initiator = initiatorClass.new(App.new, Quickfix::MemoryStoreFactory.new,
	settings(dir, 'initiator', "ConnectionType=initiator\nSocketConnectHost=127.0.0.1\nSocketConnectPort=#{port}\n" \
		"ReconnectInterval=1\nHeartBtInt=30\n[SESSION]\nBeginString=FIX.4.2\nSenderCompID=INI\nTargetCompID=ACC\n"))
idle = acceptorClass.new(App.new, Quickfix::MemoryStoreFactory.new,
	settings(dir, 'idle', "ConnectionType=acceptor\nSocketAcceptPort=#{port}\n" \
		"[SESSION]\nBeginString=FIX.4.4\nSenderCompID=IDLE\nTargetCompID=NONE\n"))

# What a transport was made with must outlive a collection (#107).
5.times { GC.start(full_mark: true, immediate_sweep: true) }

acceptor.start
initiator.start
deadline = Time.now + 20
sleep 0.05 until (initiator.isLoggedOn && acceptor.isLoggedOn) || Time.now > deadline
puts "loggedOn=#{initiator.isLoggedOn && acceptor.isLoggedOn}"
