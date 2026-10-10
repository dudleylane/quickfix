require 'rbconfig'
require 'socket'
require 'test/unit'
require 'tmpdir'

# A Ruby program that ends with an initiator or acceptor alive used to crash at
# exit: Ruby frees every object in no particular order, and one freed after its
# store factory, log factory or application used them from its destructor
# (#105). The program runs in a child process, so its exit status can be seen.
class ExitTestCase < Test::Unit::TestCase

	def test_exit_with_transports_alive
		port = TCPServer.open('127.0.0.1', 0) { |server| server.addr[1] }
		Dir.mktmpdir do |dir|
			child = File.join(__dir__, 'ExitChild.rb')
			reader, writer = IO.pipe
			pid = Process.spawn(RbConfig.ruby, '-I', File.expand_path('..', __dir__), child, dir, port.to_s,
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

			assert_match(/^loggedOn=true$/, output)
			assert(status[1].success?, "exit status #{status[1].inspect}\n#{output}")
		end
	end
end
