# Run by TransportTestCase in a process of its own: an acceptor and an initiator
# started with #start in one process, talking to each other. It prints what it
# saw as key=value lines and ends normally, so the test sees its exit status:
# a DoNotSend raised in toApp once stayed in $! and failed it (#106).
require 'quickfix_ruby'
$stdout.sync = true

dir, port, kind = ARGV
# The threaded transports call back on threads Ruby did not create (#104).
acceptorClass, initiatorClass = kind == 'threaded' ?
	[Quickfix::ThreadedSocketAcceptorBase, Quickfix::ThreadedSocketInitiatorBase] :
	[Quickfix::SocketAcceptor, Quickfix::SocketInitiator]

class App < Quickfix::Application
	attr_reader :received
	attr_accessor :refuse
	def initialize; super; @received = []; @refuse = false; end
	def onCreate(sessionID); end
	def onLogon(sessionID); end
	def onLogout(sessionID); end
	def toAdmin(message, sessionID); end
	def fromAdmin(message, sessionID); end
	def toApp(message, sessionID); raise Quickfix::DoNotSend.new if @refuse; end
	def fromApp(message, sessionID)
		clOrdID = Quickfix::ClOrdID.new
		message.getField(clOrdID)
		@received << clOrdID.getValue
	end
end

class CountingLog < Quickfix::Log
	def initialize(counts); super(); @counts = counts; end
	def clear; end
	def backup; end
	def onIncoming(s); @counts[:incoming] += 1; end
	def onOutgoing(s); @counts[:outgoing] += 1; end
	def onEvent(s); @counts[:event] += 1; end
end

class CountingLogFactory < Quickfix::LogFactory
	attr_reader :counts
	def initialize; super; @counts = Hash.new(0); @logs = []; end
	def create(*args); log = CountingLog.new(@counts); @logs << log; log; end
	def destroy(log); end
end

def settings(dir, name, body)
	File.write("#{dir}/#{name}.cfg", "[DEFAULT]\nStartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\n" + body)
	Quickfix::SessionSettings.new("#{dir}/#{name}.cfg")
end

def waitFor(seconds)
	deadline = Time.now + seconds
	sleep 0.05 until yield || Time.now > deadline
	yield
end

def order(id)
	message = Quickfix42::NewOrderSingle.new
	[Quickfix::ClOrdID.new(id), Quickfix::HandlInst.new('1'), Quickfix::Symbol.new('IBM'),
	 Quickfix::Side.new(Quickfix.Side_BUY), Quickfix::TransactTime.new,
	 Quickfix::OrdType.new(Quickfix.OrdType_MARKET)].each { |field| message.setField(field) }
	message.getHeader.setField(Quickfix::SenderCompID.new('INI'))
	message.getHeader.setField(Quickfix::TargetCompID.new('ACC'))
	Quickfix::Session.sendToTarget(message)
end

acceptorApp, initiatorApp, logFactory = App.new, App.new, CountingLogFactory.new
acceptor = acceptorClass.new(acceptorApp, Quickfix::MemoryStoreFactory.new,
	settings(dir, 'acceptor', "ConnectionType=acceptor\nSocketAcceptPort=#{port}\n" \
		"[SESSION]\nBeginString=FIX.4.2\nSenderCompID=ACC\nTargetCompID=INI\n"), logFactory)
initiator = initiatorClass.new(initiatorApp, Quickfix::MemoryStoreFactory.new,
	settings(dir, 'initiator', "ConnectionType=initiator\nSocketConnectHost=127.0.0.1\nSocketConnectPort=#{port}\n" \
		"ReconnectInterval=1\nHeartBtInt=30\n[SESSION]\nBeginString=FIX.4.2\nSenderCompID=INI\nTargetCompID=ACC\n"))

puts "application=#{acceptor.getApplication.equal?(acceptorApp) && initiator.getApplication.equal?(initiatorApp)}"
acceptorThread = acceptor.start
initiatorThread = initiator.start
puts "started=true"

ticks = 0
Thread.new { loop { ticks += 1; sleep 0.01 } }
puts "loggedOn=#{waitFor(20) { initiator.isLoggedOn && acceptor.isLoggedOn }}"

order('sent')
initiatorApp.refuse = true
order('refused')
initiatorApp.refuse = false
order('after')
waitFor(10) { acceptorApp.received.include?('after') }
puts "received=#{acceptorApp.received.join(',')}"
puts "logged=#{logFactory.counts[:incoming] > 0 && logFactory.counts[:outgoing] > 0 && logFactory.counts[:event] > 0}"
puts "ticked=#{ticks > 0}"

initiatorThread.kill
puts "killed=#{!initiatorThread.join(15).nil?}"
acceptor.stop
puts "stopped=#{acceptor.isStopped && !acceptorThread.join(15).nil?}"
