# Run by ScriptLogFactoryTestCase in a process of its own. The engine holds each Log a LogFactory creates until it
# calls destroy with it; the binding keeps the script's Log alive meanwhile and hands destroy the object create
# returned (#205).
#   drop:  a LogFactory that keeps no reference to the Log it returns. After GC, events keep reaching the logs -- on
#          the unfixed binding the collected Log's director is used and the process crashes.
#   match: a LogFactory that keeps each created Log keyed by object_id and drops it in destroy by the same id. After
#          the transport is destroyed it must hold 0 (destroy was handed the create object); unfixed, destroy gets a
#          different proxy, so the delete misses and every log is still held.
require 'quickfix_ruby'
require 'socket'
require 'tmpdir'
$stdout.sync = true
DIR = Dir.mktmpdir

class App < Quickfix::Application
  def onCreate(s); end
  def onLogon(s); end
  def onLogout(s); end
  def toAdmin(m, s); end
  def fromAdmin(m, s); end
  def toApp(m, s); end
  def fromApp(m, s); end
end

class QuietLog < Quickfix::Log
  def clear; end
  def backup; end
  def onIncoming(s); end
  def onOutgoing(s); end
  def onEvent(s); end
end

class DroppingLogFactory < Quickfix::LogFactory
  def create(*args); QuietLog.new; end    # keeps nothing
  def destroy(log); end
end

class MatchingLogFactory < Quickfix::LogFactory
  def initialize; super; @held = {}; end
  def create(*args); log = QuietLog.new; @held[log.object_id] = log; log; end
  def destroy(log); @held.delete(log.object_id); end
  def held; @held.size; end
end

# Returns one Log for every create: the binding keeps it keyed by its C++ address with a count, and must release it
# only once destroy has been called that many times.
class SharedLogFactory < Quickfix::LogFactory
  def initialize; super; @log = QuietLog.new; @creates = 0; @destroys = 0; end
  attr_reader :creates
  def create(*args); @creates += 1; @log; end
  def destroy(log); @destroys += 1 if log.equal?(@log); end   # identity: did destroy get the shared log?
  def outstanding; @creates - @destroys; end
end

# The size of the binding's own keep map on the factory (empty once every log is released).
def binding_held(factory); (factory.instance_variable_get(:@quickfixLogs)&.size) || 0; end

def free_port
  s = TCPServer.new('127.0.0.1', 0); p = s.addr[1]; s.close; p
end

def cfg(kind, port, sender, target)
  conn = kind == 'acceptor' ? "ConnectionType=acceptor\nSocketAcceptPort=#{port}\n" :
    "ConnectionType=initiator\nSocketConnectHost=127.0.0.1\nSocketConnectPort=#{port}\nReconnectInterval=1\n"
  path = File.join(DIR, "#{sender}.cfg")
  File.write(path, "[DEFAULT]\nNonStopSession=Y\nUseDataDictionary=N\nHeartBtInt=30\n" + conn +
    "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=#{sender}\nTargetCompID=#{target}\n")
  Quickfix::SessionSettings.new(path)
end

def quiet; Quickfix::ScreenLogFactory.new(false, false, false); end

mode = ARGV[0]
puts "module=#{$LOADED_FEATURES.grep(/quickfix\.so$/).first}"
port = free_port
mem = Quickfix::MemoryStoreFactory.new

case mode
when 'drop'
  acc = Quickfix::SocketAcceptor.new(App.new, mem, cfg('acceptor', port, 'ACC', 'CLI'), DroppingLogFactory.new)
  ini = Quickfix::SocketInitiator.new(App.new, mem, cfg('initiator', port, 'CLI', 'ACC'), quiet)
  acc.start; ini.start
  deadline = Time.now + 20
  sleep 0.05 until acc.isLoggedOn || Time.now > deadline
  puts "loggedOn=#{acc.isLoggedOn}"
  5.times { GC.start }                   # frees the dropped logs on the unfixed binding
  Quickfix::Session.sendToTarget(Quickfix::Message.new, Quickfix::SessionID.new('FIX.4.2', 'CLI', 'ACC'))
  sleep 3
  puts 'survived=true'
when 'match'
  factory = MatchingLogFactory.new
  acc = Quickfix::SocketAcceptor.new(App.new, mem, cfg('acceptor', port, 'ACC', 'CLI'), factory)
  created = factory.held
  Quickfix._destroyTransport(acc)        # destroys the transport -> destroy for each log
  acc = nil
  2.times { GC.start }
  puts "created=#{created}"
  puts "held_after_destroy=#{factory.held}"    # the script's own dict: destroy got the create object
  puts "binding_held=#{binding_held(factory)}" # the binding's keep map: release actually fired
  puts 'survived=true'
when 'shared'
  factory = SharedLogFactory.new
  acc = Quickfix::SocketAcceptor.new(App.new, mem, cfg('acceptor', port, 'ACC', 'CLI'), factory)
  creates = factory.creates
  Quickfix._destroyTransport(acc)
  acc = nil
  2.times { GC.start }
  puts "creates=#{creates}"                    # >= 2 (transport log + session log), all the same object
  puts "outstanding=#{factory.outstanding}"    # create - destroy (by identity); 0 means destroy got the shared log
  puts "binding_held=#{binding_held(factory)}" # the count-per-address keep released after the last destroy
  puts 'survived=true'
end
$stdout.flush
exit!(0)
