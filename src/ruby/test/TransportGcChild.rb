# Run by TransportGcTestCase in a process of its own: transports made with their
# store and log factories written inline, then dropped, so that the GC frees each
# one in the same pass as what it was made with (#201). Before, the GC could free
# a factory first, and the transport's destructor, which destroys its sessions'
# stores and logs through them, used freed memory. Prints key=value lines.
#   acceptor   SocketAcceptors, half with a LogFactory, half without
#   initiator  SocketInitiators, the same
#   threaded   ThreadedSocketAcceptorBase, the class SWIG makes, each with a
#              LogFactory written in Ruby that keeps its logs and counts destroy
#   stopped    SocketAcceptors started and stopped first
#   appcycle   each Application refers to its transport: the application is not
#              pinned (#204), so each is still freed, and its sessions go
#   factorycycle     each LogFactory, written in Ruby, refers to its transport:
#                    pinned, it keeps the transport, which is never freed until
#                    the at_exit hook deletes it, and its session stays registered
#   appfactorycycle  the same through the Application, which the LogFactory refers to
require 'quickfix_ruby'
require 'socket'
$stdout.sync = true

COUNT = 20
$destroyed = 0

class App < Quickfix::Application
	attr_accessor :transport
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

# Keeps every log it makes and counts destroy's calls. (Since #205 the binding keeps each log and hands destroy the
# object create returned -- tested in ScriptLogFactoryTestCase; this factory keeps its own only to count them here.)
class KeepingLogFactory < Quickfix::LogFactory
	attr_accessor :transport, :app
	def initialize; super; @logs = []; end
	def create(*args); log = QuietLog.new; @logs << log; log; end
	def destroy(log); $destroyed += 1; end
end

def settings(kind, index, port = 1)
	connection = kind == :acceptor ? "ConnectionType=acceptor\nSocketAcceptPort=#{port}\n" :
		"ConnectionType=initiator\nSocketConnectHost=127.0.0.1\nSocketConnectPort=#{port}\n"
	result = Quickfix::SessionSettings.new
	result.setFromString("[DEFAULT]\nNonStopSession=Y\nUseDataDictionary=N\nHeartBtInt=30\n#{connection}" \
		"[SESSION]\nBeginString=FIX.4.2\nSenderCompID=GC#{index}\nTargetCompID=TV\n")
	result
end

def logs(index)
	index.even? ? [Quickfix::ScreenLogFactory.new(false, false, false)] : []
end

def make(mode, index)
	case mode
	when 'acceptor'
		Quickfix::SocketAcceptor.new(App.new, Quickfix::MemoryStoreFactory.new, settings(:acceptor, index), *logs(index))
	when 'initiator'
		Quickfix::SocketInitiator.new(App.new, Quickfix::MemoryStoreFactory.new, settings(:initiator, index), *logs(index))
	when 'threaded'
		Quickfix::ThreadedSocketAcceptorBase.new(App.new, Quickfix::MemoryStoreFactory.new, settings(:acceptor, index),
			KeepingLogFactory.new)
	when 'stopped'
		port = TCPServer.open('127.0.0.1', 0) { |server| server.addr[1] }
		transport = Quickfix::SocketAcceptor.new(App.new, Quickfix::MemoryStoreFactory.new,
			settings(:acceptor, index, port), *logs(index))
		# start runs block() on a thread of its own, and a stop before block() has
		# begun does nothing: wait for it, then for the thread to end.
		transport.start
		deadline = Time.now + 20
		sleep 0.01 while transport.isStopped && Time.now < deadline
		transport.stop
		transport.instance_variable_get(:@quickfixThread).join(20)
		transport
	when 'appcycle'
		app = App.new
		app.transport = Quickfix::SocketAcceptor.new(app, Quickfix::MemoryStoreFactory.new, settings(:acceptor, index),
			*logs(index))
	when 'factorycycle', 'appfactorycycle'
		app, factory = App.new, KeepingLogFactory.new
		transport = Quickfix::SocketAcceptor.new(app, Quickfix::MemoryStoreFactory.new, settings(:acceptor, index), factory)
		if mode == 'factorycycle'
			factory.transport = transport
		else
			factory.app, app.transport = app, transport
		end
		transport
	else
		raise ArgumentError, mode
	end
end

def pins
	Quickfix.respond_to?(:_quickfixPinCount) ? Quickfix._quickfixPinCount : 'none'
end

# Each on a thread of its own, whose stack is gone once it has ended: the GC
# scans a live thread's stack conservatively, and a stale reference left there
# would keep a transport alive.
def onThread(&block)
	Thread.new(&block).value
end

mode = ARGV[0]
# Counted while one is alive. The block's variables outlive it if anything still
# reaches the thread, so the transport is let go before it returns.
pinned = onThread do
	kept = [make(mode, COUNT)]
	count = pins
	kept.clear
	count
end
puts "pinned=#{pinned}"
onThread do
	COUNT.times do |index|
		make(mode, index)
		GC.start(full_mark: true, immediate_sweep: true) if index % 5 == 4
	end
	nil
end
5.times { GC.start(full_mark: true, immediate_sweep: true) }
puts 'collected=true'
puts "pins=#{pins}"
puts "destroyed=#{$destroyed}"
registered = (0..COUNT).count do |index|
	Quickfix::Session.lookupSession(Quickfix::SessionID.new('FIX.4.2', "GC#{index}", 'TV'))
end
puts "registered=#{registered}"
puts 'end=true'
