# Run by CallbackErrorTestCase in a process of its own: an acceptor and an
# initiator, where one of the acceptor's callbacks raises an exception the engine
# does not expect. The binding prints it and ends the process with status 1; it
# used to call exit(1) under the engine's running threads and crash, and a Log
# that raised unwound the engine instead (#112). $stdout is left buffered, so the
# test also sees whether Ruby's buffers were flushed on the way out.
require 'quickfix_ruby'
$stdout.sync = false

dir, port, kind, where = ARGV
acceptorClass, initiatorClass = kind == 'threaded' ?
	[Quickfix::ThreadedSocketAcceptorBase, Quickfix::ThreadedSocketInitiatorBase] :
	[Quickfix::SocketAcceptor, Quickfix::SocketInitiator]

class App < Quickfix::Application
	attr_accessor :raises
	def onCreate(sessionID); end
	def onLogon(sessionID); raise 'raised in onLogon' if @raises; end
	def onLogout(sessionID); end
	def toAdmin(message, sessionID); end
	def fromAdmin(message, sessionID); end
	def toApp(message, sessionID); end
	def fromApp(message, sessionID); end
end

class RaisingLog < Quickfix::Log
	def clear; end
	def backup; end
	def onIncoming(s); raise 'raised in Log#onIncoming'; end
	def onOutgoing(s); end
	def onEvent(s); end
end

class RaisingLogFactory < Quickfix::LogFactory
	def initialize; super; @logs = []; end
	def create(*args); log = RaisingLog.new; @logs << log; log; end
	def destroy(log); end
end

def settings(dir, name, body)
	File.write("#{dir}/#{name}.cfg", "[DEFAULT]\nStartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\n" + body)
	Quickfix::SessionSettings.new("#{dir}/#{name}.cfg")
end

acceptorApp = App.new
acceptorApp.raises = where == 'onLogon'
acceptorSettings = settings(dir, 'acceptor', "ConnectionType=acceptor\nSocketAcceptPort=#{port}\n" \
	"[SESSION]\nBeginString=FIX.4.2\nSenderCompID=ACC\nTargetCompID=INI\n")
acceptor = where == 'log' ?
	acceptorClass.new(acceptorApp, Quickfix::MemoryStoreFactory.new, acceptorSettings, RaisingLogFactory.new) :
	acceptorClass.new(acceptorApp, Quickfix::MemoryStoreFactory.new, acceptorSettings)
initiator = initiatorClass.new(App.new, Quickfix::MemoryStoreFactory.new,
	settings(dir, 'initiator', "ConnectionType=initiator\nSocketConnectHost=127.0.0.1\nSocketConnectPort=#{port}\n" \
		"ReconnectInterval=1\nHeartBtInt=30\n[SESSION]\nBeginString=FIX.4.2\nSenderCompID=INI\nTargetCompID=ACC\n"))

puts 'buffered=true'
acceptor.start
initiator.start
sleep 20
puts 'survived=true'
$stdout.flush
