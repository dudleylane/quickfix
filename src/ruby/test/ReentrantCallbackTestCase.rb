require 'rbconfig'
require 'socket'
require 'test/unit'
require 'tmpdir'
require_relative 'ChildLoadPath'

# A Ruby callback may raise one of the FIX exceptions the engine expects, and the
# engine receives it as its own C++ exception, even when the same callback is
# already running -- on another thread, or nested on the same thread. The process
# keeps running and the callback dispatcher stays alive. Each case runs in a
# process of its own (ReentrantCallbackChild.rb).
class ReentrantCallbackTestCase < Test::Unit::TestCase

  # Two acceptor sessions -- one reactor, one threaded -- whose fromAdmin bodies
  # are held open at once; the threaded one raises RejectLogon while the reactor
  # one is still inside. The raise reaches the engine as a C++ throw, the
  # dispatcher thread stays alive, and the reactor session (which did not raise)
  # is let on normally.
  def test_concurrent_reentry
    out = run_child('concurrent', free_ports(2))
    assert_equal(0, out[:status].exitstatus, out[:report])
    assert_match(/^survived=true$/, out[:stdout], out[:report])
    assert_match(/^overlapped=true$/, out[:stdout], out[:report])       # the bodies really did overlap
    assert_match(/^dispatcher_alive=true$/, out[:stdout], out[:report]) # and the raise did not kill the dispatcher
    assert_match(/^reactor_logon=true$/, out[:stdout], out[:report])    # the engine kept working for the other session
  end

  # One threaded acceptor session; toApp sends from within toApp, so toApp runs
  # again on the same thread and raises DoNotSend at the inner call. DoNotSend
  # reaches the engine as a C++ throw, which drops the inner message, so the inner
  # send returns normally rather than surfacing in the outer toApp.
  def test_nested_reentry
    out = run_child('nested', free_ports(1))
    assert_equal(0, out[:status].exitstatus, out[:report])
    assert_match(/^survived=true$/, out[:stdout], out[:report])
    assert_match(/^nested_result="inner returned normally"$/, out[:stdout], out[:report])
  end

  private

  def free_ports(n)
    n.times.map { TCPServer.open('127.0.0.1', 0) { |s| s.addr[1] } }
  end

  def run_child(mode, ports)
    Dir.mktmpdir do |dir|
      child = File.join(__dir__, 'ReentrantCallbackChild.rb')
      out, err = IO.pipe, IO.pipe
      pid = Process.spawn(ChildLoadPath.environment, RbConfig.ruby, child, dir, mode, *ports.map(&:to_s),
        out: out[1], err: err[1])
      out[1].close
      err[1].close
      status = nil
      600.times do
        break if (status = Process.wait2(pid, Process::WNOHANG))
        sleep 0.1
      end
      unless status
        Process.kill('KILL', pid)
        status = Process.wait2(pid)
      end
      stdout, stderr = out[0].read, err[0].read
      report = "mode=#{mode} #{status[1].inspect}\nstdout:\n#{stdout}\nstderr:\n#{stderr}"
      { status: status[1], stdout: stdout, stderr: stderr, report: report }
    end
  end
end
