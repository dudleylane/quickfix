require 'rbconfig'
require 'test/unit'
require_relative 'ChildLoadPath'

# A transport dropped with the store and log factories it was made with, so that
# the GC frees them in the same pass, in its own order: one freed after a factory
# used it from its destructor (#201). The factories are pinned from C++ until the
# transport's C++ object has been deleted. Each case runs TransportGcChild.rb in
# a process of its own, so that a crash is a failure here.
class TransportGcTestCase < Test::Unit::TestCase

	def test_acceptor
		collected('acceptor')
	end

	def test_initiator
		collected('initiator')
	end

	def test_threaded_with_a_log_factory_written_in_ruby
		lines = collected('threaded')
		# Each transport's own log and its session's, destroyed through its factory, still alive.
		assert_equal((2 * 21).to_s, lines['destroyed'])
	end

	def test_started_and_stopped
		collected('stopped')
	end

	# The application is not pinned (#204): one that refers to its transport is
	# still freed with it.
	def test_application_referring_to_its_transport
		collected('appcycle')
	end

	# What the pins cost (#201): a factory that refers to its transport, directly
	# or through its application, keeps it, so the GC never frees the transport
	# and its sessions stay registered, until the at_exit hook deletes it. The
	# process still exits cleanly.
	def test_a_log_factory_that_refers_to_its_transport_keeps_it
		kept('factorycycle')
	end

	def test_a_log_factory_that_refers_to_its_application_keeps_the_transport
		kept('appfactorycycle')
	end

	def kept(mode)
		lines, output = child(mode)
		assert_equal('1', lines['pinned'], output)
		assert_equal('21', lines['pins'], output)
		assert_equal('21', lines['registered'], output)
	end

	def collected(mode)
		lines, output = child(mode)
		# Held from the transport's construction until its C++ object was deleted,
		# and only until then.
		assert_equal('1', lines['pinned'], output)
		assert_equal('0', lines['pins'], output)
		# Deleted, so its sessions are unregistered.
		assert_equal('0', lines['registered'], output)
		lines
	end

	def child(mode)
		reader, writer = IO.pipe
		pid = Process.spawn(ChildLoadPath.environment, RbConfig.ruby, File.join(__dir__, 'TransportGcChild.rb'), mode,
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
		assert(status[1].success?, "exit status #{status[1].inspect}\n#{output}")
		assert_no_match(/Sanitizer/, output)
		lines = output.lines.map(&:chomp).select { |line| line.include?('=') }.to_h { |line| line.split('=', 2) }
		assert_equal('true', lines['end'], output)
		[lines, output]
	end
end
