require 'rbconfig'
require 'socket'
require 'test/unit'
require 'tmpdir'
require_relative 'ChildLoadPath'

# #close ends a transport deterministically (#207): a script whose factory or application refers to its
# transport -- which the #201 pin keeps alive, and its sessions registered -- can close it rather than wait
# for the GC or the at_exit hook. close stops it and deletes it; afterward a second transport for the same
# SessionID constructs, a second close is a no-op, and the proxy raises. It refuses a call from a callback or
# the transport's own thread, which would delete it under its own stack. Each child runs in a process of its
# own, so a transport left running cannot take the suite down with it.
class CloseTestCase < Test::Unit::TestCase

	def test_close_unregisters_a_reactor_acceptors_session
		lines, status, output = runChild('reactor')
		assert_equal('Quickfix::ConfigError', lines['second_before_close'], output)
		assert_equal('constructed', lines['second_after_close'], output)
		assert_equal('nil', lines['second_close'], output)
		assert_equal('ObjectPreviouslyDeleted', lines['method_after_close'], output)
		assert(status.success?, "exit status #{status.inspect}\n#{output}")
	end

	def test_close_unregisters_a_threaded_acceptors_session
		lines, status, output = runChild('threaded')
		assert_equal('Quickfix::ConfigError', lines['second_before_close'], output)
		assert_equal('constructed', lines['second_after_close'], output)
		assert_equal('nil', lines['second_close'], output)
		assert_equal('ObjectPreviouslyDeleted', lines['method_after_close'], output)
		assert(status.success?, "exit status #{status.inspect}\n#{output}")
	end

	def test_close_from_a_reactor_callback_raises
		lines, status, output = runChild('callback_reactor')
		assert_match(/\AThreadError:/, lines['callback'], output)
		assert(status.success?, "exit status #{status.inspect}\n#{output}")
	end

	def test_close_from_a_threaded_callback_raises
		lines, status, output = runChild('callback_threaded')
		assert_match(/\AThreadError:/, lines['callback'], output)
		assert(status.success?, "exit status #{status.inspect}\n#{output}")
	end

	def test_close_unregisters_a_logged_on_acceptor
		lines, status, output = runChild('logged_on')
		assert_equal('true', lines['logged_on'], output)
		assert_equal('constructed', lines['second_after_close'], output)
		assert(status.success?, "exit status #{status.inspect}\n#{output}")
	end

	def test_close_while_another_thread_is_in_block
		lines, status, output = runChild('while_block')
		assert_equal('closed', lines['while_block_close'], output)
		assert_equal('true', lines['blocker_done'], output)
		assert(status.success?, "exit status #{status.inspect}\n#{output}")
	end

	def test_close_from_a_sendtotarget_toApp_raises
		lines, status, output = runChild('sendtotarget')
		assert_equal('ThreadError', lines['callback_close'], output)      # the GVL-held in-callback guard
		assert_equal('returned', lines['sendtotarget'], output)           # sendToTarget did not run on freed memory
		assert_equal('closed', lines['close_from_main'], output)          # still closable from outside the callback
		assert_equal('0', lines['pins'], output)
		assert(status.success?, "exit status #{status.inspect}\n#{output}")
	end

	def test_close_after_a_raising_toApp_succeeds
		lines, status, output = runChild('donotsend')
		# The in-callback guard decremented as DoNotSend unwound through it, so close() from the main
		# thread afterward does not see a callback still in progress: it succeeds rather than raise.
		assert_equal('closed', lines['close_from_main'], output)
		assert_equal('0', lines['pins'], output)
		assert(status.success?, "exit status #{status.inspect}\n#{output}")
	end

	def test_close_from_onLogout_does_not_deadlock
		lines, status, output = runChild('logout_close')
		assert_equal('true', lines['logged_on'], output)
		assert_equal('ThreadError', lines['logout_close'], output)        # the callback's close refused at once
		assert_equal('closed', lines['main_close'], output)               # not "timed out": no #111 deadlock
		assert(status.success?, "exit status #{status.inspect}\n#{output}")
	end

	def test_close_an_initiator
		lines, status, output = runChild('initiator')
		assert_equal('ObjectPreviouslyDeleted', lines['method_after_close'], output)
		assert_equal('0', lines['pins'], output)                          # the #201 pin returned to baseline
		assert(status.success?, "exit status #{status.inspect}\n#{output}")
	end

	def test_close_reports_a_still_running_transport
		lines, status, output = runChild('running')
		assert_match(/\ARuntimeError:/, lines['running'], output)         # raised rather than freed under block()
		assert_equal('closed', lines['cleaned'], output)
		assert(status.success?, "exit status #{status.inspect}\n#{output}")
	end

	def test_close_reports_a_failed_start
		lines, status, output = runChild('failed_start')
		assert_match(/Error\z/, lines['failed_start'], output)            # the bind failure, re-raised by close
		assert_equal('ObjectPreviouslyDeleted', lines['method_after_close'], output)  # deleted despite it (#123)
		assert(status.success?, "exit status #{status.inspect}\n#{output}")
	end

	def runChild(mode)
		Dir.mktmpdir do |dir|
			child = File.join(__dir__, 'CloseChild.rb')
			reader, writer = IO.pipe
			pid = Process.spawn(ChildLoadPath.environment, RbConfig.ruby, child, dir, mode, out: writer, err: writer)
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
