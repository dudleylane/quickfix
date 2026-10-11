require 'rbconfig'
require 'socket'
require 'test/unit'
require 'tmpdir'

# An exception a Ruby callback lets out, other than the FIX exceptions the engine
# expects, ends the process with status 1 and the exception printed as Ruby
# prints an uncaught one. It used to crash or hang the process (#112). The child
# runs in a process of its own.
class CallbackErrorTestCase < Test::Unit::TestCase

	def test_application_callback
		checkExit('socket', 'onLogon', 'raised in onLogon')
	end

	def test_application_callback_threaded
		checkExit('threaded', 'onLogon', 'raised in onLogon')
	end

	def test_log
		checkExit('socket', 'log', 'raised in Log#onIncoming')
	end

	def test_log_threaded
		checkExit('threaded', 'log', 'raised in Log#onIncoming')
	end

	def checkExit(kind, where, message)
		port = TCPServer.open('127.0.0.1', 0) { |server| server.addr[1] }
		Dir.mktmpdir do |dir|
			child = File.join(__dir__, 'CallbackErrorChild.rb')
			out, err = IO.pipe, IO.pipe
			pid = Process.spawn(RbConfig.ruby, '-I', File.expand_path('..', __dir__), child, dir, port.to_s, kind, where,
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
			report = "#{status[1].inspect}\nstdout:\n#{stdout}\nstderr:\n#{stderr}"

			assert_equal(1, status[1].exitstatus, report)
			assert_match(/#{Regexp.escape(message)} \(RuntimeError\)/, stderr, report)
			assert_match(/CallbackErrorChild\.rb:\d+:in/, stderr, report)
			# Ruby's own $stdout buffer was flushed on the way out.
			assert_match(/^buffered=true$/, stdout, report)
			assert_no_match(/^survived=/, stdout, report)
		end
	end
end
