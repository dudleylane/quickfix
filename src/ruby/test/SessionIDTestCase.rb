require 'quickfix_ruby'
require 'test/unit'

# A SessionID is equal by value, and the one the binding hands Ruby -- a callback's argument, LogFactory#create's,
# Session#getSessionID's result -- is Ruby's own copy, never a view of the engine's: a session's ID is freed with
# its transport (#193). Each test with a transport uses an initiator that is never started: making it makes its
# session, which calls onCreate and the LogFactory, and deleting it deletes the session.
class SessionIDTestCase < Test::Unit::TestCase

	class Keeper < Quickfix::Application
		attr_reader :kept
		def initialize(change = false); super(); @kept = []; @change = change; end
		def onCreate(sessionID)
			@kept << sessionID
			sessionID.fromString('FIX.4.2:CHANGED->BYAPP') if @change
		end
		def onLogon(sessionID); end
		def onLogout(sessionID); end
		def toAdmin(message, sessionID); end
		def toApp(message, sessionID); end
		def fromAdmin(message, sessionID); end
		def fromApp(message, sessionID); end
	end

	class NullLog < Quickfix::Log
		def clear; end
		def backup; end
		def onIncoming(s); end
		def onOutgoing(s); end
		def onEvent(s); end
	end

	class KeepingLogFactory < Quickfix::LogFactory
		attr_reader :kept
		def initialize; super; @kept = []; @logs = []; end
		def create(*args); @kept.concat(args); log = NullLog.new; @logs << log; log; end
		def destroy(log); end
	end

	def settings(sender)
		settings = Quickfix::SessionSettings.new
		settings.setFromString("[DEFAULT]\nConnectionType=initiator\nSocketConnectHost=127.0.0.1\nSocketConnectPort=1\n" \
			"StartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\nHeartBtInt=30\n" \
			"[SESSION]\nBeginString=FIX.4.2\nSenderCompID=#{sender}\nTargetCompID=ACC\n")
		settings
	end

	def test_equal_by_value
		# Each was a distinct object, equal only to itself, with its own hash.
		a, b = Quickfix::SessionID.new('FIX.4.2', 'A', 'B'), Quickfix::SessionID.new('FIX.4.2', 'A', 'B')
		other = Quickfix::SessionID.new('FIX.4.2', 'A', 'C')
		assert(a == b)
		assert(a.eql?(b))
		assert(!(a != b))
		assert_equal(a.hash, b.hash)
		assert(a != other)
		assert(a != Quickfix::SessionID.new('FIX.4.2', 'A', 'B', 'QUALIFIER'))
		assert_equal(1, { a => 1 }[b])
		assert([a].include?(b))
		assert_equal([], [a].reject { |x| x == b })
		list = [a]
		list.delete(b)
		assert_equal([], list)
		# Another type compares unequal rather than raising.
		assert(!(a == 'FIX.4.2:A->B'))
		assert(!a.eql?(nil))
		assert(!['FIX.4.2:A->B', 1].include?(a))
	end

	def test_a_handed_sessionID_equals_one_made_by_hand
		application = Keeper.new
		initiator = Quickfix::SocketInitiator.new(application, Quickfix::MemoryStoreFactory.new, settings('EQUAL'),
			KeepingLogFactory.new)
		sessionID = Quickfix::SessionID.new('FIX.4.2', 'EQUAL', 'ACC')
		assert_equal([sessionID], application.kept)
		assert_equal(sessionID, Quickfix::Session.lookupSession(sessionID).getSessionID)
		assert_equal(1, { application.kept[0] => 1 }[sessionID])
		assert(Quickfix._destroyInitiator(initiator))
	end

	def test_changing_a_handed_sessionID_leaves_the_sessions_own
		# A view was the session's own ID: onCreate's change, and this one, changed the engine's.
		initiator = Quickfix::SocketInitiator.new(Keeper.new(true), Quickfix::MemoryStoreFactory.new, settings('CHANGER'),
			KeepingLogFactory.new)
		session = Quickfix::Session.lookupSession(Quickfix::SessionID.new('FIX.4.2', 'CHANGER', 'ACC'))
		assert_equal('FIX.4.2:CHANGER->ACC', session.getSessionID.to_s)
		session.getSessionID.fromString('FIX.4.2:CHANGED->BYAPP')
		assert_equal('FIX.4.2:CHANGER->ACC', session.getSessionID.to_s)
		assert(Quickfix._destroyInitiator(initiator))
	end

	def test_a_handed_sessionID_outlives_its_transport
		application, logFactory = Keeper.new, KeepingLogFactory.new
		initiator = Quickfix::SocketInitiator.new(application, Quickfix::MemoryStoreFactory.new, settings('KEEPER'),
			logFactory)
		sessionID = Quickfix::SessionID.new('FIX.4.2', 'KEEPER', 'ACC')
		kept = application.kept + logFactory.kept + [Quickfix::Session.lookupSession(sessionID).getSessionID]
		assert_equal(3, kept.size)
		# As the at_exit hook deletes a transport: deleting it deletes its session.
		assert(Quickfix._destroyInitiator(initiator))
		assert_nil(Quickfix::Session.lookupSession(sessionID))
		# Freed memory, while these were views.
		assert_equal(['FIX.4.2:KEEPER->ACC'] * 3, kept.map(&:to_s))
	end
end
