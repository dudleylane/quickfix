# Run by ExitTestCase in a process of its own: an acceptor started on a port
# something else already listens on, so its start fails on the thread start
# made. The program then ends. Ruby's at_exit hook joined that thread, which
# re-raised the start's error, and the rescue around it skipped deleting the
# transport, which Ruby's finalizers then freed out of order and crashed on
# (#123).
require 'quickfix_ruby'
require 'socket'
$stdout.sync = true

dir, port, kind = ARGV
acceptorClass = kind == 'threaded' ? Quickfix::ThreadedSocketAcceptorBase : Quickfix::SocketAcceptor

class App < Quickfix::Application
	def onCreate(sessionID); end
	def onLogon(sessionID); end
	def onLogout(sessionID); end
	def toAdmin(message, sessionID); end
	def fromAdmin(message, sessionID); end
	def toApp(message, sessionID); end
	def fromApp(message, sessionID); end
end

blocker = TCPServer.new('0.0.0.0', port.to_i)
File.write("#{dir}/acceptor.cfg", "[DEFAULT]\nStartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\n" \
	"ConnectionType=acceptor\nSocketAcceptPort=#{port}\n[SESSION]\nBeginString=FIX.4.2\nSenderCompID=ACC\nTargetCompID=INI\n")
acceptor = acceptorClass.new(App.new, Quickfix::MemoryStoreFactory.new, Quickfix::SessionSettings.new("#{dir}/acceptor.cfg"))
thread = acceptor.start
thread.report_on_exception = false
begin
	thread.join(20)
	puts 'startFailed=false'
rescue StandardError => error
	puts "startFailed=#{error.class}"
end
