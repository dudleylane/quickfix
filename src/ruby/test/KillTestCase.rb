require 'rbconfig'
require 'socket'
require 'test/unit'
require 'tmpdir'
require_relative 'ChildLoadPath'

# Thread#kill on a transport's thread stops it (#102), even when the kill lands
# inside a Ruby callback, where it used to end the process with status 1 (#147).
class KillTestCase < Test::Unit::TestCase

	def test_kill_inside_a_callback
		port = TCPServer.open('127.0.0.1', 0) { |server| server.addr[1] }
		Dir.mktmpdir do |dir|
			child = File.join(__dir__, 'KillChild.rb')
			reader, writer = IO.pipe
			pid = Process.spawn(ChildLoadPath.environment, RbConfig.ruby, child, dir, port.to_s,
				out: writer, err: writer)
			writer.close
			status = nil
			900.times do
				break if (status = Process.wait2(pid, Process::WNOHANG))
				sleep 0.1
			end
			unless status
				Process.kill('KILL', pid)
				status = Process.wait2(pid)
			end
			output = reader.read
			reader.close
			report = "#{status[1].inspect}\n#{output}"
			assert_match(/^inCallback=true$/, output, report)
			assert_match(/^killed=true$/, output, report)
			assert_match(/^stopped=true$/, output, report)
			# The kill still ends the thread, as it would outside a callback.
			assert_match(/^ensured=true$/, output, report)
			assert_no_match(/^afterBlock=/, output, report)
			assert_match(/^status=false$/, output, report)
			assert_match(/^ended=true$/, output, report)
			assert(status[1].success?, report)
		end
	end
end
