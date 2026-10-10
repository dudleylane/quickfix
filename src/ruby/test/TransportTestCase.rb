require 'rbconfig'
require 'socket'
require 'test/unit'
require 'tmpdir'

# Initiator#start and Acceptor#start run the transport on a Ruby thread. Before
# #102 that thread held the GVL for as long as the transport ran, so the rest of
# the program never ran again. The transports run in a child process, which a
# hang there cannot take the suite down with.
class TransportTestCase < Test::Unit::TestCase

	def test_start_leaves_ruby_running
		checkTransports('socket')
	end

	def test_start_leaves_ruby_running_threaded
		checkTransports('threaded')
	end

	def checkTransports(kind)
		port = TCPServer.open('127.0.0.1', 0) { |server| server.addr[1] }
		Dir.mktmpdir do |dir|
			child = File.join(__dir__, 'TransportChild.rb')
			reader, writer = IO.pipe
			pid = Process.spawn(RbConfig.ruby, '-I', File.expand_path('..', __dir__), child, dir, port.to_s, kind,
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

			# The adapter the engine holds is not what Ruby gets back.
			assert_equal('true', lines['application'], output)
			assert_equal('true', lines['started'], output)
			assert_equal('true', lines['loggedOn'], output)
			# A message toApp refuses with DoNotSend is not sent.
			assert_equal('sent,after', lines['received'], output)
			# A Log implemented in Ruby is called from the transport's thread.
			assert_equal('true', lines['logged'], output)
			# Another Ruby thread ran while the transports did.
			assert_equal('true', lines['ticked'], output)
			# Thread#kill ends a transport's thread, and stop returns.
			assert_equal('true', lines['killed'], output)
			assert_equal('true', lines['stopped'], output)
			# Nothing a callback raised for the engine is left in $! (#106).
			assert(status[1].success?, "exit status #{status[1].inspect}\n#{output}")
		end
	end
end
