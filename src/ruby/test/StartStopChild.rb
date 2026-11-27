# Run by StartStopTestCase in a process of its own. Ruby's Initiator#start and
# Acceptor#start return only once the transport is running, or its thread has
# ended because block() raised (#206); before, they returned at once, so a
# stop() right after start() found the transport still marked stopped and did
# nothing, and block() then ran until exit. Each mode starts a transport, stops
# it at once, and reports whether it actually stopped. Modes cover the reactor
# and threaded acceptor and initiator, since each has its own stop path.
require 'quickfix_ruby'
require 'socket'
$stdout.sync = true

dir, port, mode = ARGV

class App < Quickfix::Application
	def onCreate(sessionID); end
	def onLogon(sessionID); end
	def onLogout(sessionID); end
	def toAdmin(message, sessionID); end
	def fromAdmin(message, sessionID); end
	def toApp(message, sessionID); end
	def fromApp(message, sessionID); end
end

def settings(dir, name, body)
	File.write("#{dir}/#{name}.cfg", "[DEFAULT]\nNonStopSession=Y\nUseDataDictionary=N\n" + body)
	Quickfix::SessionSettings.new("#{dir}/#{name}.cfg")
end

quiet = Quickfix::ScreenLogFactory.new(false, false, false)
acceptorBody = "ConnectionType=acceptor\nSocketAcceptPort=#{port}\n" \
	"[SESSION]\nBeginString=FIX.4.2\nSenderCompID=ACC\nTargetCompID=INI\n"
# Nothing listens on the port, so the initiator's block() loops reconnecting until stop.
initiatorBody = "ConnectionType=initiator\nSocketConnectHost=127.0.0.1\nSocketConnectPort=#{port}\n" \
	"ReconnectInterval=1\nHeartBtInt=30\n[SESSION]\nBeginString=FIX.4.2\nSenderCompID=INI\nTargetCompID=ACC\n"

transport =
	case mode
	when 'acceptor'
		Quickfix::SocketAcceptor.new(App.new, Quickfix::MemoryStoreFactory.new, settings(dir, 'a', acceptorBody), quiet)
	when 'threaded_acceptor'
		Quickfix::ThreadedSocketAcceptorBase.new(App.new, Quickfix::MemoryStoreFactory.new,
			settings(dir, 'a', acceptorBody), quiet)
	when 'initiator'
		Quickfix::SocketInitiator.new(App.new, Quickfix::MemoryStoreFactory.new, settings(dir, 'i', initiatorBody), quiet)
	when 'threaded_initiator'
		Quickfix::ThreadedSocketInitiatorBase.new(App.new, Quickfix::MemoryStoreFactory.new,
			settings(dir, 'i', initiatorBody), quiet)
	end

transport.start
transport.stop                      # at once, in the window #206 is about
thread = transport.instance_variable_get(:@quickfixThread)
joined = thread.join(10)            # fixed: the thread has ended; unfixed: still running block()
puts "stopped=#{transport.isStopped}"
puts "thread_dead=#{!joined.nil?}"
if mode.include?('acceptor')
	connectable = (TCPSocket.new('127.0.0.1', port.to_i).close; true) rescue false
	puts "connectable=#{connectable}"
end
