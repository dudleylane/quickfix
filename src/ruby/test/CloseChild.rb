# Run by CloseTestCase in a process of its own. #close ends a transport deterministically (#207): it
# stops it and deletes it, so its sessions are unregistered (a later transport for the same SessionID
# then constructs), its factories are let go, and its views end. Each mode prints key=value lines.
require 'quickfix_ruby'
require 'socket'
$stdout.sync = true

dir, mode = ARGV

class App < Quickfix::Application
	attr_accessor :on_logon, :on_logout, :on_toapp
	def onCreate(sessionID); end
	def onLogon(sessionID); @on_logon&.call; end
	def onLogout(sessionID); @on_logout&.call; end
	def toAdmin(message, sessionID); end
	def fromAdmin(message, sessionID); end
	def toApp(message, sessionID); @on_toapp&.call; end
	def fromApp(message, sessionID); end
end

def app_message
	msg = Quickfix::Message.new
	msg.getHeader.setField(Quickfix::StringField.new(35, 'D')) # MsgType=D, an app message -> toApp
	msg
end

class QuietLog < Quickfix::Log
	def clear; end
	def backup; end
	def onIncoming(s); end
	def onOutgoing(s); end
	def onEvent(s); end
end

# Refers to its transport, so the #201 pin keeps it (and its sessions registered) until #close.
class KeepingLogFactory < Quickfix::LogFactory
	attr_accessor :transport
	def create(*args); QuietLog.new; end
	def destroy(log); end
end

def free_port
	TCPServer.open('127.0.0.1', 0) { |s| s.addr[1] }
end

def settings(dir, name, body)
	File.write("#{dir}/#{name}.cfg", "[DEFAULT]\nNonStopSession=Y\nUseDataDictionary=N\n" + body)
	Quickfix::SessionSettings.new("#{dir}/#{name}.cfg")
end

def acceptor_body(port, sender = 'ACC', target = 'INI')
	"ConnectionType=acceptor\nSocketAcceptPort=#{port}\n[SESSION]\nBeginString=FIX.4.2\n" \
		"SenderCompID=#{sender}\nTargetCompID=#{target}\n"
end

def initiator_body(port)
	"ConnectionType=initiator\nSocketConnectHost=127.0.0.1\nSocketConnectPort=#{port}\nReconnectInterval=1\n" \
		"HeartBtInt=30\n[SESSION]\nBeginString=FIX.4.2\nSenderCompID=INI\nTargetCompID=ACC\n"
end

quiet = Quickfix::ScreenLogFactory.new(false, false, false)
store = -> { Quickfix::MemoryStoreFactory.new }

case mode
when 'reactor', 'threaded'
	# A factory that refers to its transport keeps it (and ACC->INI registered) until close. After close
	# a second acceptor for the same SessionID constructs; a second close is a no-op; a method raises.
	klass = mode == 'reactor' ? Quickfix::SocketAcceptor : Quickfix::ThreadedSocketAcceptorBase
	port = free_port
	f = KeepingLogFactory.new
	t = klass.new(App.new, store.call, settings(dir, 'a', acceptor_body(port)), f)
	f.transport = t
	t.start
	GC.start
	before = (klass.new(App.new, store.call, settings(dir, 'b', acceptor_body(port)), quiet); 'constructed') rescue $!.class.name
	t.close
	after = (klass.new(App.new, store.call, settings(dir, 'c', acceptor_body(port)), quiet); 'constructed') rescue $!.class.name
	puts "second_before_close=#{before}"
	puts "second_after_close=#{after}"
	puts "second_close=#{t.close.inspect}"
	puts "method_after_close=#{(t.isStopped; 'ok') rescue $!.class.name}"

when 'callback_reactor', 'callback_threaded'
	# onLogon calls close on the transport calling it: it would delete the transport under its own stack,
	# so close raises ThreadError, as stop refuses a threaded callback (#111).
	aklass = mode == 'callback_reactor' ? Quickfix::SocketAcceptor : Quickfix::ThreadedSocketAcceptorBase
	iklass = mode == 'callback_reactor' ? Quickfix::SocketInitiator : Quickfix::ThreadedSocketInitiatorBase
	port = free_port
	result = Queue.new
	app = App.new
	acceptor = aklass.new(app, store.call, settings(dir, 'a', acceptor_body(port)), quiet)
	app.on_logon = lambda do
		begin
			acceptor.close
			result << 'closed'
		rescue ThreadError => e
			result << "ThreadError: #{e.message[0, 40]}"
		rescue => e
			result << "#{e.class}: #{e.message[0, 40]}"
		end
	end
	initiator = iklass.new(App.new, store.call, settings(dir, 'i', initiator_body(port)), quiet)
	acceptor.start
	initiator.start
	waited = Thread.new { result.pop }
	puts "callback=#{waited.join(30) ? waited.value : 'timed out'}"
	Thread.new { acceptor.stop rescue nil }.join(20)
	Thread.new { initiator.stop rescue nil }.join(20)

when 'logged_on'
	# A logged-on reactor acceptor, closed from another thread: its session is unregistered, so a second
	# acceptor for it constructs.
	port = free_port
	acceptor = Quickfix::SocketAcceptor.new(App.new, store.call, settings(dir, 'a', acceptor_body(port)), quiet)
	initiator = Quickfix::SocketInitiator.new(App.new, store.call, settings(dir, 'i', initiator_body(port)), quiet)
	acceptor.start
	initiator.start
	deadline = Time.now + 20
	sleep 0.05 while !acceptor.isLoggedOn && Time.now < deadline
	logged_on = acceptor.isLoggedOn
	initiator.stop
	acceptor.close
	after = (Quickfix::SocketAcceptor.new(App.new, store.call, settings(dir, 'c', acceptor_body(port)), quiet); 'constructed') rescue $!.class.name
	puts "logged_on=#{logged_on}"
	puts "second_after_close=#{after}"

when 'while_block'
	# close while another thread is inside block(): close stops it, so block() returns and the thread
	# is joined before the delete.
	port = free_port
	acceptor = Quickfix::SocketAcceptor.new(App.new, store.call, settings(dir, 'a', acceptor_body(port)), quiet)
	blocker = Thread.new { acceptor.block rescue nil }
	sleep 0.05 while acceptor.isStopped && blocker.alive?
	closed = (acceptor.close; 'closed') rescue $!.class.name
	# @quickfixThread is nil here (block ran on `blocker`, not via start), so close joined no start thread;
	# block() returned when close stopped it.
	puts "while_block_close=#{closed}"
	puts "blocker_done=#{!blocker.join(5).nil?}"

when 'sendtotarget'
	# A toApp reached through Session.sendToTarget runs on the caller's own thread, holding the GVL
	# (withGvl's first path). close() from it would delete the transport under sendToTarget's stack -- a
	# use-after-free -- so the in-callback guard makes it raise; sendToTarget then returns, and the
	# transport is still closable from the main thread.
	port = free_port
	app = App.new
	ini = Quickfix::SocketInitiator.new(app, store.call, settings(dir, 'i', initiator_body(port)), quiet)
	callback = nil
	app.on_toapp = lambda { callback = (ini.close; 'closed(no-refusal)') rescue $!.class.name }
	sid = Quickfix::SessionID.new('FIX.4.2', 'INI', 'ACC')
	sent = (Quickfix::Session.sendToTarget(app_message, sid); 'returned') rescue $!.class.name
	puts "callback_close=#{callback}"
	puts "sendtotarget=#{sent}"
	puts "close_from_main=#{(ini.close; 'closed') rescue $!.class.name}"

when 'donotsend'
	# A toApp that raises DoNotSend (which the engine expects) unwinds as a C++ exception through
	# withGvl's in-callback guard. The guard's destructor must decrement, or close() from the main thread
	# afterward would see a callback still in progress and raise. close_from_main succeeds proves it did.
	port = free_port
	app = App.new
	ini = Quickfix::SocketInitiator.new(app, store.call, settings(dir, 'i', initiator_body(port)), quiet)
	app.on_toapp = lambda { raise Quickfix::DoNotSend.new }
	sid = Quickfix::SessionID.new('FIX.4.2', 'INI', 'ACC')
	sent = (Quickfix::Session.sendToTarget(app_message, sid); 'returned') rescue $!.class.name
	puts "donotsend=#{sent}"
	puts "close_from_main=#{(ini.close; 'closed') rescue $!.class.name}"

when 'logout_close'
	# A threaded acceptor logged on, closed from the main thread. close()'s stop() logs the session out,
	# so onLogout runs on the callback thread while main holds the close mutex. Its close() must raise at
	# once -- the in-callback refusal is before the mutex -- rather than block on the mutex, which is held
	# by the main thread whose stop() is joining the connection thread this callback runs for (#111).
	# Without that, main's close() would deadlock.
	port = free_port
	accApp = App.new
	acceptor = Quickfix::ThreadedSocketAcceptorBase.new(accApp, store.call, settings(dir, 'a', acceptor_body(port)), quiet)
	initiator = Quickfix::ThreadedSocketInitiatorBase.new(App.new, store.call, settings(dir, 'i', initiator_body(port)), quiet)
	hook = Queue.new
	accApp.on_logout = lambda { hook << ((acceptor.close; 'closed(no-refusal)') rescue $!.class.name) }
	acceptor.start
	initiator.start
	deadline = Time.now + 20
	sleep 0.05 while !acceptor.isLoggedOn && Time.now < deadline
	logged_on = acceptor.isLoggedOn
	main_close = Thread.new { (acceptor.close; 'closed') rescue $!.class.name }
	done = main_close.join(20)
	Thread.new { initiator.stop rescue nil }.join(20)
	puts "logged_on=#{logged_on}"
	puts "logout_close=#{hook.empty? ? 'no-callback' : hook.pop}"
	puts "main_close=#{done ? main_close.value : 'timed out'}"

when 'initiator'
	# close an initiator (the other transport kind): its session unregisters, so a second initiator for it
	# constructs, a method afterward raises, and the pin is released.
	port = free_port
	f = KeepingLogFactory.new
	ini = Quickfix::SocketInitiator.new(App.new, store.call, settings(dir, 'i', initiator_body(port)), f)
	f.transport = ini
	ini.start
	GC.start
	ini.close
	puts "method_after_close=#{(ini.isStopped; 'ok') rescue $!.class.name}"

when 'running'
	# A transport still blocking when close's timeout expires is not deleted: close raises rather than free
	# it under the running block(). block() runs on another thread; a zero-timeout close cannot see it stop
	# in time, so it reports it still running. A proper close from the main thread then cleans up.
	port = free_port
	acceptor = Quickfix::SocketAcceptor.new(App.new, store.call, settings(dir, 'a', acceptor_body(port)), quiet)
	blocker = Thread.new { acceptor.block rescue nil }
	sleep 0.05 while acceptor.isStopped && blocker.alive?
	sleep 0.2
	running = (acceptor.close(0); 'closed') rescue "#{$!.class}:#{$!.message[0, 30]}"
	blocker.join(10)
	cleaned = (acceptor.close; 'closed') rescue $!.class.name
	puts "running=#{running}"
	puts "cleaned=#{cleaned}"

when 'failed_start'
	# start() on a port already bound fails in the thread #start made; close joins that thread, re-raises
	# the failure -- after deleting the transport (#123) -- and the transport is gone afterward.
	port = free_port
	held = TCPServer.new('127.0.0.1', port)
	acceptor = Quickfix::SocketAcceptor.new(App.new, store.call, settings(dir, 'a', acceptor_body(port)), quiet)
	acceptor.start
	sleep 0.5
	failed = (acceptor.close; 'closed(no-error)') rescue $!.class.name
	after = (acceptor.isStopped; 'ok') rescue $!.class.name
	held.close
	puts "failed_start=#{failed}"
	puts "method_after_close=#{after}"
end

puts "pins=#{Quickfix._quickfixPinCount}"
