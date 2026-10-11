# Run by StopTestCase in a process of its own: a threaded acceptor whose onLogon
# stops it. Called there, stop would join the connection thread waiting for that
# onLogon and never return (#111), so it raises ThreadError. "rescued" rescues
# it, "thread" stops the acceptor from a thread of its own, which works, and
# "unrescued" lets the error out of the callback, which ends the process with
# status 1 (#112).
require 'quickfix_ruby'
$stdout.sync = true

dir, port, mode = ARGV

class App < Quickfix::Application
	attr_accessor :onLogonHook
	def onCreate(sessionID); end
	def onLogon(sessionID); @onLogonHook&.call; end
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
acceptor = Quickfix::ThreadedSocketAcceptorBase.new(acceptorApp, Quickfix::MemoryStoreFactory.new,
	settings(dir, 'acceptor', "ConnectionType=acceptor\nSocketAcceptPort=#{port}\n" \
		"[SESSION]\nBeginString=FIX.4.2\nSenderCompID=ACC\nTargetCompID=INI\n"))
initiator = Quickfix::ThreadedSocketInitiatorBase.new(App.new, Quickfix::MemoryStoreFactory.new,
	settings(dir, 'initiator', "ConnectionType=initiator\nSocketConnectHost=127.0.0.1\nSocketConnectPort=#{port}\n" \
		"ReconnectInterval=1\nHeartBtInt=30\n[SESSION]\nBeginString=FIX.4.2\nSenderCompID=INI\nTargetCompID=ACC\n"))

result = Queue.new
acceptorApp.onLogonHook = lambda do
	case mode
	when 'rescued'
		begin
			acceptor.stop
			result << 'returned'
		rescue ThreadError => error
			result << "ThreadError: #{error.message}"
		end
	when 'thread'
		Thread.new { acceptor.stop; result << 'stopped' }
	when 'unrescued'
		acceptor.stop
	end
end

acceptor.start
initiator.start
waited = Thread.new { result.pop }
puts "callback=#{waited.join(30) ? waited.value : 'timed out'}"
initiator.stop
acceptor.stop
puts "stopped=#{acceptor.isStopped && initiator.isStopped}"
