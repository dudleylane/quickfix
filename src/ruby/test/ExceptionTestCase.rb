require 'quickfix_ruby'
require 'test/unit'

# An exception an engine call throws reaches Ruby as its own class, through tryRubyException and raiseCaught
# (../quickfix.i), and once it has, std::current_exception() finds none outside a handler. A Thread#raise or a
# Thread#kill that lands while a call ends views goes on in place of what the call then throws (#147, #196), with the
# same result.
class ExceptionTestCase < Test::Unit::TestCase

	NoPartyIDs = Quickfix44::NewOrderSingle::NoPartyIDs

	# What Quickfix._throw(kind) raises, by kind: each exception tryRubyException has a handler for, in the order of
	# its handlers, then a std::exception and an int.
	KINDS = [Quickfix::DataDictionaryNotFound, Quickfix::FieldNotFound, Quickfix::FieldConvertError,
	         Quickfix::MessageParseError, Quickfix::InvalidMessage, Quickfix::ConfigError, Quickfix::RuntimeError,
	         Quickfix::InvalidTagNumber, Quickfix::RequiredTagMissing, Quickfix::TagNotDefinedForMessage,
	         Quickfix::NoTagValue, Quickfix::IncorrectTagValue, Quickfix::IncorrectDataFormat,
	         Quickfix::IncorrectMessageStructure, Quickfix::DuplicateFieldNumber, Quickfix::InvalidMessageType,
	         Quickfix::UnsupportedMessageType, Quickfix::UnsupportedVersion, Quickfix::TagOutOfOrder,
	         Quickfix::RepeatedTag, Quickfix::RepeatingGroupCountMismatch, Quickfix::DoNotSend, Quickfix::RejectLogon,
	         Quickfix::SessionNotFound, Quickfix::IOException, Quickfix::SocketSendFailed, Quickfix::SocketRecvFailed,
	         Quickfix::SocketCloseFailed, Quickfix::SocketException, Quickfix::Exception, ::RuntimeError,
	         ::RuntimeError]

	class Interrupted < StandardError; end

	def test_each_exception_reaches_ruby_as_its_own_class
		assert_false(Quickfix._currentException, 'an exception is current before any was thrown')
		KINDS.each_with_index do |type, kind|
			error = assert_raise(type, "kind #{kind}") { Quickfix._throw(kind) }
			assert_equal(type, error.class, "kind #{kind}")
			assert_false(Quickfix._currentException, "std::current_exception() is set after #{type} (kind #{kind})")
		end
		assert_equal(44, assert_raise(Quickfix::FieldNotFound) { Quickfix._throw(1) }.field)
		assert_equal('a std::exception', assert_raise(::RuntimeError) { Quickfix._throw(KINDS.size - 2) }.message)
		assert_equal('unknown exception', assert_raise(::RuntimeError) { Quickfix._throw(KINDS.size - 1) }.message)
		assert_nil(Quickfix._throw(KINDS.size))
		assert_false(Quickfix._currentException)
	end

	# Each pass ends the view getGroupRef registered, in setString, which then throws InvalidMessage.
	def failing_churn(message)
		group = NoPartyIDs.new
		group.setField(Quickfix::PartyID.new('A'))
		loop do
			message.addGroup(group)
			message.getGroupRef(1, 453)
			begin
				message.setString('not FIX')
			rescue Quickfix::InvalidMessage
			end
		end
	end

	def test_a_raise_that_lands_while_a_failing_call_ends_views_goes_on_in_its_place
		landed, raises = Quickfix._viewsLanded, 30
		received = 0
		current = []
		worker = Thread.new do
			message = Quickfix::Message.new
			Thread.handle_interrupt(Interrupted => :never) do
				while received < raises
					begin
						Thread.handle_interrupt(Interrupted => :immediate) { failing_churn(message) }
					rescue Interrupted
						received += 1
						current << Quickfix._currentException
					end
				end
			end
		end
		raises.times do
			sleep 0.01
			worker.raise(Interrupted)
		end
		assert_not_nil(worker.join(120), 'every raise arrived')
		assert_equal(raises, received)
		assert_equal([false] * raises, current, 'std::current_exception() after each raise')
		assert_operator(Quickfix._viewsLanded, :>, landed, 'no raise landed in the registry: the test proves nothing')
	ensure
		worker&.kill
	end

	def test_a_kill_that_lands_while_a_failing_call_ends_views_ends_the_thread
		landed, kills = Quickfix._viewsLanded, 20
		current = []
		kills.times do
			worker = Thread.new do
				failing_churn(Quickfix::Message.new)
			ensure
				current << Quickfix._currentException
			end
			sleep 0.01
			worker.kill
			assert_not_nil(worker.join(60), 'the thread ended')
			# false: it ended as a killed thread does, not by an exception.
			assert_equal(false, worker.status)
		end
		assert_equal([false] * kills, current, 'std::current_exception() as each killed thread ended')
		assert_operator(Quickfix._viewsLanded, :>, landed, 'no kill landed in the registry: the test proves nothing')
	end
end
