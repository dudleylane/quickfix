# Run by SynchronizedApplicationTestCase in a process of its own: an application
# wrapped in a SynchronizedApplication with no other reference to it, then
# collected, then called through the wrapper. The wrapper keeps the application
# (#208), so the calls reach it and the process survives; before, the collected
# application was used from freed memory and the process crashed. A crash is a
# failure in the parent.
require 'quickfix_ruby'
$stdout.sync = true

class App < Quickfix::Application
	def onCreate(sessionID); end
	def onLogon(sessionID); end
	def onLogout(sessionID); end
	def toAdmin(message, sessionID); end
	def fromAdmin(message, sessionID); end
	def toApp(message, sessionID); end
	def fromApp(message, sessionID); end
end

# The only reference to the application is the one the wrapper keeps.
sync = Quickfix::SynchronizedApplication.new(App.new)
5.times { GC.start }
sessionID = Quickfix::SessionID.new('FIX.4.2', 'SENDER', 'TARGET')
message = Quickfix::Message.new
1000.times do
	sync.onLogon(sessionID)
	sync.fromApp(message, sessionID)
end
puts 'end=true'
$stdout.flush
