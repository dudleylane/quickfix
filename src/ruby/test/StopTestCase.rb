require 'rbconfig'
require 'socket'
require 'test/unit'
require 'tmpdir'

# A threaded transport's callbacks run on the "quickfix callbacks" thread while
# the connection thread waits for them, so stop called from one would wait for
# itself (#111). It raises ThreadError instead, and stopping from another thread
# works. The child runs in a process of its own, which a hang cannot take the
# suite down with.
class StopTestCase < Test::Unit::TestCase

	def test_stop_from_callback_raises
		lines, status, output = runChild('rescued')
		assert_match(/\AThreadError: Quickfix::ThreadedSocketAcceptorBase#stop cannot be called from a callback/,
			lines['callback'], output)
		assert_equal('true', lines['stopped'], output)
		assert(status.success?, "exit status #{status.inspect}\n#{output}")
	end

	def test_stop_from_callback_thread
		lines, status, output = runChild('thread')
		assert_equal('stopped', lines['callback'], output)
		assert_equal('true', lines['stopped'], output)
		assert(status.success?, "exit status #{status.inspect}\n#{output}")
	end

	# Left out of the callback, the ThreadError ends the process with status 1, as
	# any exception a callback lets out does (#112).
	def test_stop_from_callback_unrescued
		lines, status, output = runChild('unrescued')
		assert_equal(1, status.exitstatus, "exit status #{status.inspect}\n#{output}")
		assert_match(/cannot be called from a callback.*\(ThreadError\)/, output)
		assert_nil(lines['callback'], output)
	end

	def runChild(mode)
		port = TCPServer.open('127.0.0.1', 0) { |server| server.addr[1] }
		Dir.mktmpdir do |dir|
			child = File.join(__dir__, 'StopChild.rb')
			reader, writer = IO.pipe
			pid = Process.spawn(RbConfig.ruby, '-I', File.expand_path('..', __dir__), child, dir, port.to_s, mode,
				out: writer, err: writer)
			writer.close
			status = nil
			600.times do
				break if (status = Process.wait2(pid, Process::WNOHANG))
				sleep 0.1
			end
			unless status
				Process.kill('KILL', pid)
				status = Process.wait2(pid)
			end
			output = reader.read
			reader.close
			lines = output.lines.map(&:chomp).select { |line| line.include?('=') }.to_h { |line| line.split('=', 2) }
			[lines, status[1], output]
		end
	end
end
