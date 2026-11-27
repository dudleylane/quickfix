require 'rbconfig'
require 'socket'
require 'test/unit'
require 'tmpdir'
require_relative 'ChildLoadPath'

# Ruby's Initiator#start and Acceptor#start return only once block() has entered
# and marked the transport started, or its start thread has ended because block()
# raised (#206). Before, they returned at once, so a stop() right after start()
# found the transport still marked stopped and did nothing, leaving block() to run
# until exit. Each child starts a transport, stops it at once, and must find it
# stopped and its thread ended -- for the reactor and threaded acceptor and
# initiator. (A threaded acceptor's listener could survive a stop() that raced
# block()'s bind; that race is fixed and guarded in ut, #211.) In a process of its
# own, so a transport left running cannot take the suite down with it.
class StartStopTestCase < Test::Unit::TestCase

	def test_stop_right_after_start_stops_an_acceptor
		lines, status, output = runChild('acceptor')
		assert_equal('true', lines['stopped'], output)
		assert_equal('true', lines['thread_dead'], output)
		assert_equal('false', lines['connectable'], output)
		assert(status.success?, "exit status #{status.inspect}\n#{output}")
	end

	def test_stop_right_after_start_stops_an_initiator
		lines, status, output = runChild('initiator')
		assert_equal('true', lines['stopped'], output)
		assert_equal('true', lines['thread_dead'], output)
		assert(status.success?, "exit status #{status.inspect}\n#{output}")
	end

	def test_stop_right_after_start_stops_a_threaded_acceptor
		lines, status, output = runChild('threaded_acceptor')
		assert_equal('true', lines['stopped'], output)
		assert_equal('true', lines['thread_dead'], output)
		assert_equal('false', lines['connectable'], output)
		assert(status.success?, "exit status #{status.inspect}\n#{output}")
	end

	def test_stop_right_after_start_stops_a_threaded_initiator
		lines, status, output = runChild('threaded_initiator')
		assert_equal('true', lines['stopped'], output)
		assert_equal('true', lines['thread_dead'], output)
		assert(status.success?, "exit status #{status.inspect}\n#{output}")
	end

	def runChild(mode)
		port = TCPServer.open('127.0.0.1', 0) { |server| server.addr[1] }
		Dir.mktmpdir do |dir|
			child = File.join(__dir__, 'StartStopChild.rb')
			reader, writer = IO.pipe
			pid = Process.spawn(ChildLoadPath.environment, RbConfig.ruby, child, dir, port.to_s, mode,
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
