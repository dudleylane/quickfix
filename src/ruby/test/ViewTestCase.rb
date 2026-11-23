require 'quickfix_ruby'
require 'test/unit'

# What a call returns from inside another object outlives nothing it points into (#194). A view -- a message's
# header, trailer or group, a SessionSettings' dictionary -- holds its owner; a const view -- a field, a SessionID's
# BeginString -- is the caller's own copy; a call whose result is its argument returns the argument. Each was a
# proxy that owned nothing and kept nothing alive, so one kept past its owner read and wrote freed memory.
class ViewTestCase < Test::Unit::TestCase

	def order
		message = Quickfix44::NewOrderSingle.new
		message.setField(Quickfix::Price.new(1.5))
		party = Quickfix44::NewOrderSingle::NoPartyIDs.new
		party.setField(Quickfix::PartyID.new('PARTY'))
		sub = Quickfix44::NewOrderSingle::NoPartyIDs::NoPartySubIDs.new
		sub.setField(Quickfix::PartySubID.new('SUB'))
		party.addGroup(sub)
		message.addGroup(party)
		message
	end

	def owner(view)
		view.instance_variable_get(:@quickfixOwner)
	end

	def sweep
		GC.start(full_mark: true, immediate_sweep: true)
	end

	def test_header_and_trailer_keep_their_message
		message = Quickfix::Message.new
		assert_same(message, owner(message.getHeader))
		assert_same(message, owner(message.getTrailer))
		# So a chained call is safe: the messages these made live as long as their headers, across a sweep.
		headers = Array.new(200) { Quickfix::Message.new.getHeader }
		trailers = Array.new(200) { Quickfix::Message.new.getTrailer }
		sweep
		headers.each { |header| header.setField(Quickfix::MsgType.new('D')); assert_equal('D', header.getField(35)) }
		trailers.each { |trailer| trailer.setField(Quickfix::CheckSum.new(123)); assert_equal('123', trailer.getField(10)) }
	end

	def test_group_views_keep_their_message
		[:getGroupRef, :getGroupPtr].each do |method|
			message = order
			view = message.send(method, 1, 453)
			assert_same(message, owner(view), method.to_s)
			# A view is what C++ returns: a write through it reaches the message.
			view.setField(Quickfix::PartyRole.new(3))
			assert_equal('3', message.getGroupRef(1, 453).getField(452), method.to_s)
			views = Array.new(100) { order.send(method, 1, 453) }
			sweep
			views.each { |kept| assert_equal('PARTY', kept.getField(448), method.to_s) }
		end
	end

	def test_settings_views_keep_their_settings
		settings = Quickfix::SessionSettings.new
		settings.setFromString("[DEFAULT]\nConnectionType=initiator\nHeartBtInt=30\n" \
			"[SESSION]\nBeginString=FIX.4.2\nSenderCompID=S\nTargetCompID=T\n")
		sessionID = Quickfix::SessionID.new('FIX.4.2', 'S', 'T')
		defaults, section = settings.get, settings.get(sessionID)
		assert_same(settings, owner(defaults))
		assert_same(settings, owner(section))
		# Writing through get() reaches the settings, as it did.
		section.setString('HeartBtInt', '45')
		assert_equal('45', settings.get(sessionID).getString('HeartBtInt'))
	end

	def test_const_views_are_copies
		[:getFieldRef, :getFieldPtr].each do |method|
			message = Quickfix::Message.new
			message.setField(Quickfix::Price.new(1.5))
			field = message.send(method, 44)
			# The caller's own: writing it changes nothing else, and it outlives the message growing, which moves
			# the message's fields.
			field.setString('2')
			assert_equal('1.5', message.getField(44), method.to_s)
			(5000...5100).each { |tag| message.setField(tag, 'x') }
			assert_equal('2', field.getString, method.to_s)
		end
	end

	def test_session_id_getters_are_copies
		sessionID = Quickfix::SessionID.new('FIX.4.2', 'SENDER', 'TARGET')
		fields = [sessionID.getBeginString, sessionID.getSenderCompID, sessionID.getTargetCompID]
		fields[1].setString('CHANGED')
		assert_equal('SENDER', sessionID.getSenderCompID.getString)
		assert_equal(['FIX.4.2', 'CHANGED', 'TARGET'], fields.map(&:getString))
	end

	def test_a_result_that_is_the_argument_is_the_argument
		message = order
		price = Quickfix::Price.new
		assert_same(price, message.getField(price))
		assert_equal(1.5, message.getField(Quickfix::Price.new).getValue)
		assert_equal('1.5', message.getField(44))
		party = Quickfix44::NewOrderSingle::NoPartyIDs.new
		assert_same(party, message.getGroup(1, party))
		assert_equal('PARTY', message.getGroup(1, Quickfix44::NewOrderSingle::NoPartyIDs.new).getField(448))
		sub = Quickfix44::NewOrderSingle::NoPartyIDs::NoPartySubIDs.new
		assert_same(sub, party.getGroup(1, sub))
		assert_equal('SUB', sub.getField(523))
		plain = Quickfix::FieldMap.new
		assert_same(plain, Quickfix::FieldMap.instance_method(:getGroup).bind_call(message, 1, 453, plain))
		assert_equal('PARTY', plain.getField(448))
		hop = Quickfix::Group.new(627, 628)
		hop.setField(628, 'HOP')
		[message.getHeader, message.getTrailer].each do |part|
			part.addGroup(hop)
			got = Quickfix::Group.new(627, 628)
			assert_same(got, part.getGroup(1, got))
			assert_equal('HOP', got.getField(628))
		end
	end

	def test_internals_are_not_wrapped
		{ Quickfix::FieldMap => [:groups, :cloneInto], Quickfix::Group => [:cloneInto],
		  Quickfix::DataDictionary => [:getOrderedFields, :getHeaderOrderedFields, :getTrailerOrderedFields,
		                               :getMessageOrderedFields],
		  Quickfix::SocketAcceptorBase => [:sessionToPort] }.each do |klass, names|
			names.each { |name| assert(!klass.method_defined?(name), "#{klass}##{name}") }
		end
		assert(!Quickfix.const_defined?(:GroupArena))
	end
end
