require 'rbconfig'
require 'test/unit'
require_relative 'ChildLoadPath'

# A SynchronizedApplication keeps the application it wraps (#208), so an
# application dropped by the script is still reached through the wrapper rather
# than used from freed memory. The case runs in a process of its own, so that a
# crash is a failure here.
class SynchronizedApplicationTestCase < Test::Unit::TestCase

	def test_wrapped_application_survives_collection
		reader, writer = IO.pipe
		pid = Process.spawn(ChildLoadPath.environment, RbConfig.ruby,
			File.join(__dir__, 'SynchronizedApplicationChild.rb'), out: writer, err: writer)
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
		assert(status[1].success?, "exit status #{status[1].inspect}\n#{output}")
		assert_no_match(/Sanitizer/, output)
		assert_match(/^end=true$/, output, output)
	end
end
