require 'rbconfig'
require 'test/unit'
require_relative 'ChildLoadPath'

# The engine holds each Log a LogFactory creates until destroy; the binding keeps the script's Log alive meanwhile
# and hands destroy the object create returned (#205). Each case runs in a process of its own, so that a crash is a
# failure here.
class ScriptLogFactoryTestCase < Test::Unit::TestCase

	# A factory that keeps nothing: after GC, events still reach its logs because the binding keeps them.
	def test_a_factory_that_keeps_nothing
		lines = child('drop')
		assert_equal('true', lines['loggedOn'], lines.inspect)
	end

	# destroy is handed the object create returned (the script's own dict empties), and the binding releases its own
	# hold on it (its keep map empties).
	def test_destroy_gets_the_created_log
		lines = child('match')
		assert_equal('0', lines['held_after_destroy'], lines.inspect)
		assert_equal('0', lines['binding_held'], lines.inspect)
	end

	# One Log returned for every create: kept once per address with a count, released only after the last destroy.
	def test_a_shared_log
		lines = child('shared')
		assert_equal('0', lines['outstanding'], lines.inspect)
		assert_equal('0', lines['binding_held'], lines.inspect)
	end

	private

	def child(mode)
		reader, writer = IO.pipe
		pid = Process.spawn(ChildLoadPath.environment, RbConfig.ruby,
			File.join(__dir__, 'ScriptLogFactoryChild.rb'), mode, out: writer, err: writer)
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
		assert_equal('true', lines['survived'], output)
		lines
	end
end
