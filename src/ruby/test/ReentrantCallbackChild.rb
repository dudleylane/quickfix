# Run by ReentrantCallbackTestCase in a process of its own. A Ruby callback may
# raise one of the FIX exceptions the engine expects, and the engine receives it
# as its own C++ exception, even when the same callback is already running
# elsewhere -- on another thread, or nested on the same thread. The process keeps
# running and the callback dispatcher stays alive.
#
#   concurrent PORT_R PORT_T: a reactor acceptor session (callbacks on the
#     transport's own thread) and a threaded acceptor session (callbacks on the
#     shared dispatcher) share one application. A latch holds the two fromAdmin
#     bodies open at once; the threaded session's fromAdmin then raises RejectLogon
#     while the reactor session's is still inside.
#   nested PORT: one threaded acceptor session; toApp of an outer message sends an
#     inner message, so toApp runs again on the same thread and raises DoNotSend at
#     the inner call.
#
# Prints 'survived=true' and 'dispatcher_alive=<bool>' on the way out.
require 'quickfix_ruby'
$stdout.sync = true

$m = Mutex.new
$cv = ConditionVariable.new
$r_inside = false      # the reactor session's fromAdmin is running
$t_overlapped = false  # the threaded session's fromAdmin raised while the reactor's was inside
$release_r = false     # the reactor session's fromAdmin may return
$done = false          # the scenario has played out; later callbacks are no-ops
$r_logon = false       # the reactor session reached onLogon
$nested_result = nil   # what the inner toApp send raised

# Wait until the block is true or the deadline passes; $m must be held.
def wait_until(deadline)
  loop do
    return true if yield
    left = deadline - Time.now
    return false if left <= 0
    $cv.wait($m, left)
  end
end

class NoOpApp < Quickfix::Application
  def onCreate(s); end
  def onLogon(s); end
  def onLogout(s); end
  def toAdmin(m, s); end
  def toApp(m, s); end
  def fromApp(m, s); end
  def fromAdmin(m, s); end
end

# The acceptor application for the concurrent case.
class OverlapApp < Quickfix::Application
  def onCreate(s); end
  def onLogon(s)
    $m.synchronize do
      $r_logon = true if s.getTargetCompID.getValue == 'CLIR'
      $cv.broadcast
    end
  end
  def onLogout(s); end
  def toAdmin(m, s); end
  def toApp(m, s); end
  def fromApp(m, s); end

  def fromAdmin(message, sessionID)
    return unless message.getHeader.getField(35) == 'A'
    case sessionID.getTargetCompID.getValue
    when 'CLIR'                       # reactor session: go inside and stay there
      $m.synchronize do
        return if $done
        $r_inside = true
        $cv.broadcast
        wait_until(Time.now + 10) { $release_r || $done }
        $r_inside = false             # cleared on the way out, so a threaded entry that arrives late sees no
      end                             # stale overlap and does not raise at depth 1 (a vacuous pass)
      # returns normally -> this session is accepted
    when 'CLIT'                       # threaded session: raise while the reactor one is inside
      overlapped = false
      $m.synchronize do
        return if $done
        wait_until(Time.now + 10) { $r_inside }
        overlapped = $r_inside
        $t_overlapped = true if overlapped
        $cv.broadcast
      end
      raise Quickfix::RejectLogon.new('refused') if overlapped
    end
  end
end

# The acceptor application for the nested case.
class NestApp < Quickfix::Application
  attr_reader :session
  def onCreate(s); end
  def onLogon(s); $m.synchronize { @session = s; $cv.broadcast }; end
  def onLogout(s); end
  def toAdmin(m, s); end
  def fromAdmin(m, s); end
  def fromApp(m, s); end
  def toApp(message, s)
    head = message.isSetField(148) ? message.getField(148) : '?'
    if head == 'OUTER'
      inner = Quickfix::Message.new
      inner.getHeader.setField(Quickfix::MsgType.new('B'))
      inner.setField(Quickfix::Headline.new('INNER'))
      begin
        Quickfix::Session.sendToTarget(inner, s)   # -> toApp(INNER) on this same thread, re-entered
        r = 'inner returned normally'
      rescue => e
        r = "inner raised #{e.class}"
      end
      $m.synchronize { $nested_result = r; $cv.broadcast }
    elsif head == 'INNER'
      raise Quickfix::DoNotSend.new
    end
  end
end

def settings(dir, name, body)
  File.write("#{dir}/#{name}.cfg",
    "[DEFAULT]\nStartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\nReconnectInterval=1\nHeartBtInt=30\n" + body)
  Quickfix::SessionSettings.new("#{dir}/#{name}.cfg")
end

def acc_cfg(port, sender, target)
  "ConnectionType=acceptor\nSocketAcceptPort=#{port}\n" \
  "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=#{sender}\nTargetCompID=#{target}\n"
end

def ini_cfg(port, sender, target)
  "ConnectionType=initiator\nSocketConnectHost=127.0.0.1\nSocketConnectPort=#{port}\n" \
  "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=#{sender}\nTargetCompID=#{target}\n"
end

def dispatcher_alive?
  t = Quickfix._callbackThread
  !t.nil? && !t.status.nil? && t.status != false
end

dir, mode, *ports = ARGV
mem = Quickfix::MemoryStoreFactory.new

case mode
when 'concurrent'
  rport, tport = ports
  app = OverlapApp.new
  # reactor acceptor (session ACCR->CLIR): its callbacks run on the transport's own thread
  racc = Quickfix::SocketAcceptor.new(app, mem,
    settings(dir, 'racc', acc_cfg(rport, 'ACCR', 'CLIR')))
  # threaded acceptor (session ACCT->CLIT): its callbacks run on the dispatcher
  tacc = Quickfix::ThreadedSocketAcceptor.new(app, mem,
    settings(dir, 'tacc', acc_cfg(tport, 'ACCT', 'CLIT')))
  rini = Quickfix::SocketInitiator.new(NoOpApp.new, mem,
    settings(dir, 'rini', ini_cfg(rport, 'CLIR', 'ACCR')))
  tini = Quickfix::SocketInitiator.new(NoOpApp.new, mem,
    settings(dir, 'tini', ini_cfg(tport, 'CLIT', 'ACCT')))
  racc.start; tacc.start; rini.start; tini.start
  puts 'started'
  # Wait for the overlap to play out: the threaded fromAdmin raised while the reactor one was inside.
  ok = $m.synchronize { wait_until(Time.now + 20) { $t_overlapped } }
  sleep 0.5                               # let the raise propagate through the engine
  alive = dispatcher_alive?
  $m.synchronize { $release_r = true; $done = true; $cv.broadcast }
  sleep 1.0                               # settle; let the reactor fromAdmin return
  puts "overlapped=#{ok}"
  puts "reactor_logon=#{$r_logon}"
  puts "dispatcher_alive=#{alive}"
  puts 'survived=true'
  $stdout.flush
  exit!(0)

when 'nested'
  tport = ports[0]
  app = NestApp.new
  tacc = Quickfix::ThreadedSocketAcceptor.new(app, mem,
    settings(dir, 'tacc', acc_cfg(tport, 'ACCT', 'CLIT')))
  tini = Quickfix::SocketInitiator.new(NoOpApp.new, mem,
    settings(dir, 'tini', ini_cfg(tport, 'CLIT', 'ACCT')))
  tacc.start; tini.start
  puts 'started'
  session = $m.synchronize { wait_until(Time.now + 20) { app.session }; app.session }
  if session
    outer = Quickfix::Message.new
    outer.getHeader.setField(Quickfix::MsgType.new('B'))
    outer.setField(Quickfix::Headline.new('OUTER'))
    sent = Quickfix::Session.sendToTarget(outer, session)
    puts "outer_sent=#{sent}"
  else
    puts 'outer_sent=no-session'
  end
  $m.synchronize { wait_until(Time.now + 10) { $nested_result } }
  alive = dispatcher_alive?
  puts "nested_result=#{$nested_result.inspect}"
  puts "dispatcher_alive=#{alive}"
  puts 'survived=true'
  $stdout.flush
  exit!(0)

else
  warn "unknown mode #{mode.inspect}"
  exit!(2)
end
