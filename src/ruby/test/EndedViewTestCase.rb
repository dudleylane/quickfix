require 'quickfix_ruby'
require 'test/unit'
require 'timeout'

# A group's view -- getGroupRef's, getGroupPtr's -- points into its message, so a call through the binding that
# destroys the group ends the view first: its pointer becomes null, and SWIG raises ObjectPreviouslyDeleted for it
# instead of reaching freed memory (#196). Each destroying call is tested through every overload, with the views it
# must end and those it must leave, and every public method of the classes groups live in is classified, so that a
# new one is classified too. The binding keeps its views in a registry that calls into Ruby, where a thread switch,
# a Thread#raise or a Thread#kill can land; what lands there reaches the thread once the call is out of C++.
class EndedViewTestCase < Test::Unit::TestCase

	NoPartyIDs = Quickfix44::NewOrderSingle::NoPartyIDs
	NoPartySubIDs = Quickfix44::NewOrderSingle::NoPartyIDs::NoPartySubIDs
	NAMES = [:a, :a1, :b, :b1, :header, :trailer]
	BODY = [:a, :a1, :b, :b1]

	# Every public method of the classes a group lives in: what destroys a group a view may point at, and what does
	# not.
	DESTROYS = {
		Quickfix::FieldMap => [:clear, :removeField, :removeGroup, :replaceGroup, :getGroup, :swap],
		Quickfix::Message => [:clear, :setString, :setStringHeader, :removeGroup, :replaceGroup, :getGroup, :reverseRoute],
		Quickfix::Header => [:removeGroup, :replaceGroup, :getGroup],
		Quickfix::Trailer => [:removeGroup, :replaceGroup, :getGroup],
		Quickfix::Group => [:removeGroup, :replaceGroup, :getGroup],
	}
	KEEPS = {
		Quickfix::FieldMap => [:addGroup, :begin, :calculateLength, :calculateString, :calculateTotal, :end, :g_begin,
		                       :g_end, :getField, :getFieldIfSet, :getFieldPtr, :getFieldRef, :getGroupPtr, :getGroupRef,
		                       :groupCount, :hasGroup, :isEmpty, :isSetField, :setField, :totalFields],
		Quickfix::Message => [:InitializeXML, :addGroup, :bodyLength, :checkSum, :getHeader, :getSessionID, :getTrailer,
		                      :hasEmbeddedSOH, :hasGroup, :hasOutOfOrderGroupMembers, :hasValidStructure, :isAdmin,
		                      :isAdminMsgType, :isApp, :isEmpty, :isHeaderField, :isTrailerField, :setGroup,
		                      :setSessionID, :toApplVerID, :toBeginString, :toString, :toXML, :to_s],
		Quickfix::Header => [:addGroup, :hasGroup],
		Quickfix::Trailer => [:addGroup, :hasGroup],
		Quickfix::Group => [:addGroup, :delim, :field, :hasGroup],
	}

	class Interrupted < StandardError; end

	class App < Quickfix::Application
		def onCreate(sessionID); end
		def onLogon(sessionID); end
		def onLogout(sessionID); end
		def toAdmin(message, sessionID); end
		def toApp(message, sessionID); end
		def fromAdmin(message, sessionID); end
		def fromApp(message, sessionID); end
	end

	def party(identifier, sub)
		group = NoPartyIDs.new
		group.setField(Quickfix::PartyID.new(identifier))
		child = NoPartySubIDs.new
		child.setField(Quickfix::PartySubID.new(sub))
		group.addGroup(child)
		group
	end

	# A group of 628, at tag: NoHops by default, or any tag a test needs a group at.
	def hop(identifier, tag = 627)
		group = Quickfix::Group.new(tag, 628)
		group.setField(628, identifier)
		group
	end

	# Two parties, each with a sub ID, and a group in the header and in the trailer.
	def message
		result = Quickfix44::NewOrderSingle.new
		result.addGroup(party('A', 'A1'))
		result.addGroup(party('B', 'B1'))
		result.getHeader.addGroup(hop('H'))
		result.getTrailer.addGroup(hop('T'))
		result
	end

	def wire
		source = Quickfix::Message.new
		source.getHeader.setField(Quickfix::BeginString.new('FIX.4.4'))
		source.getHeader.setField(Quickfix::MsgType.new('D'))
		source.setField(Quickfix::ClOrdID.new('X'))
		source.toString
	end

	# owner, holding a group at tag, and a view of that group.
	def holding(owner, tag = 9000)
		if owner.is_a?(Quickfix::Group)
			owner.addGroup(hop('X', tag))
		else
			owner.addGroup(tag, hop('X', tag))
		end
		[owner, owner.getGroupRef(1, tag)]
	end

	# A view of every group in a message: getGroupRef's and getGroupPtr's, of a group and of one under it.
	def views(message)
		a = message.getGroupRef(1, 453)
		b = message.getGroupPtr(2, 453)
		{ a: a, a1: a.getGroupRef(1, 802), b: b, b1: b.getGroupPtr(1, 802),
		  header: message.getHeader.getGroupRef(1, 627), trailer: message.getTrailer.getGroupRef(1, 627) }
	end

	def fieldMap(method, receiver, *args)
		Quickfix::FieldMap.instance_method(method).bind_call(receiver, *args)
	end

	def assert_ended(view, message = nil)
		# assert_raise_kind_of takes a String last as the message, and anything else as another class to expect.
		error = assert_raise_kind_of(*[RuntimeError, message].compact) { view.totalFields }
		assert_equal('ObjectPreviouslyDeleted', error.class.name, message)
	end

	# The views named in ended raise; the others still read their group.
	def assert_views(views, ended, message = nil)
		NAMES.each do |name|
			if ended.include?(name)
				assert_ended(views[name], "#{message}: #{name}")
			else
				views[name].totalFields
			end
		end
	end

	# Runs the block in a thread of its own, which it fails unless the block finishes within seconds.
	def within(seconds)
		thread = Thread.new { yield }
		assert_not_nil(thread.join(seconds), "finished within #{seconds} s")
		thread.value
	ensure
		thread&.kill
	end

	# A call that registers a view, one that ends it, and one that puts its group back: the registry's own calls
	# into Ruby, in a loop that never ends by itself. What lands in removeGroup is raised once it has removed the
	# group, before addGroup puts it back, so each pass first puts back what an earlier one did not.
	def churn(message)
		remover, again = NoPartyIDs.new, party('A', 'A1')
		loop do
			message.addGroup(again) while message.groupCount(453) < 2
			message.getGroupRef(1, 453)
			message.removeGroup(1, remover)
			message.addGroup(again)
		end
	end

	def test_clear
		m, other = message, message
		v, w = views(m), views(other)
		v[:a].clear
		assert_views(v, [:a1], "a group's clear")
		assert_equal(0, v[:a].totalFields)
		header = m.getHeader
		m.clear
		assert_views(v, NAMES, 'Message#clear')
		assert_views(w, [], 'another message')
		# The header is no group: its view stays.
		assert_equal(0, header.totalFields)
		plain, view = holding(Quickfix::FieldMap.new)
		plain.clear
		assert_ended(view)
	end

	def test_set_string
		text = wire
		[->(m) { m.setString(text) }, ->(m) { m.setString(text, false) }, ->(m) { m.setString(text, false, nil) },
		 ->(m) { m.setString(text, false, nil, nil) }, ->(m) { m.setStringHeader(text) }].each do |call|
			m = message
			v = views(m)
			call.(m)
			assert_views(v, NAMES)
			assert_equal('D', m.getHeader.getField(35))
		end
		# A string it refuses ends them too: it clears before it parses.
		m = message
		v = views(m)
		assert_raise(Quickfix::InvalidMessage) { m.setString('garbage') }
		assert_views(v, NAMES)
	end

	def test_remove_field
		m = message
		v = views(m)
		m.removeField(9999)
		assert_views(v, [], 'a field that is not set')
		m.removeField(453)
		assert_views(v, BODY, "a group's count")
		m.getHeader.removeField(627)
		assert_views(v, BODY + [:header])
		m.getTrailer.removeField(627)
		assert_views(v, NAMES)
	end

	def test_remove_group
		[[->(m) { fieldMap(:removeGroup, m, 1, 453) }, [:a, :a1]],
		 [->(m) { fieldMap(:removeGroup, m, 453) }, BODY],
		 [->(m) { fieldMap(:removeGroup, m, 3, 453) }, []],
		 [->(m) { fieldMap(:removeGroup, m, 9000) }, []],
		 [->(m) { m.removeGroup(2, NoPartyIDs.new) }, [:b, :b1]],
		 [->(m) { m.removeGroup(NoPartyIDs.new) }, BODY],
		 [->(m) { m.getHeader.removeGroup(1, Quickfix::Group.new(627, 628)) }, [:header]],
		 [->(m) { m.getHeader.removeGroup(Quickfix::Group.new(627, 628)) }, [:header]],
		 [->(m) { m.getTrailer.removeGroup(1, Quickfix::Group.new(627, 628)) }, [:trailer]],
		 [->(m) { m.getTrailer.removeGroup(Quickfix::Group.new(627, 628)) }, [:trailer]]].each do |call, ended|
			m = message
			v = views(m)
			call.(m)
			assert_views(v, ended, ended.inspect)
		end
		# A group's own: the group, and every group under it.
		[[->(g) { g.removeGroup(1, NoPartySubIDs.new) }, false], [->(g) { g.removeGroup(NoPartySubIDs.new) }, true]].each do |call, all|
			owner = NoPartyIDs.new
			2.times { owner.addGroup(holding(NoPartySubIDs.new)[0]) }
			first, second = owner.getGroupRef(1, 802), owner.getGroupRef(2, 802)
			deep, deep2 = first.getGroupRef(1, 9000), second.getGroupRef(1, 9000)
			call.(owner)
			assert_ended(first)
			assert_ended(deep)
			if all
				assert_ended(second)
				assert_ended(deep2)
			else
				assert_equal('X', deep2.getField(628))
			end
		end
	end

	def test_replace_group
		# The group stays, with what replaced it; the groups under it go.
		m = message
		v = views(m)
		fieldMap(:replaceGroup, m, 1, 453, party('C', 'C1'))
		assert_views(v, [:a1])
		assert_equal('C', v[:a].getField(448))
		assert_equal('C1', v[:a].getGroupRef(1, 802).getField(523))
		m.replaceGroup(2, party('D', 'D1'))
		assert_views(v, [:a1, :b1])
		assert_equal('D', v[:b].getField(448))
		m.getHeader.replaceGroup(1, hop('H2'))
		m.getTrailer.replaceGroup(1, hop('T2'))
		assert_views(v, [:a1, :b1])
		assert_equal(['H2', 'T2'], [v[:header].getField(628), v[:trailer].getField(628)])
		# One that is not there: nothing.
		m = message
		v = views(m)
		fieldMap(:replaceGroup, m, 3, 453, party('E', 'E1'))
		m.replaceGroup(3, party('E', 'E1'))
		assert_views(v, [])
		# A group's own.
		owner = party('P', 'P1')
		sub = owner.getGroupRef(1, 802)
		owner.replaceGroup(1, holding(NoPartySubIDs.new)[0])
		deeper = sub.getGroupRef(1, 9000)
		owner.replaceGroup(1, NoPartySubIDs.new)
		sub.totalFields
		assert_ended(deeper)
	end

	def test_get_group
		# FieldMap's and Group's assign over the target, and only when the source is there.
		m = message
		target, view = holding(Quickfix::FieldMap.new)
		assert_raise(Quickfix::FieldNotFound) { fieldMap(:getGroup, m, 9, 453, target) }
		view.totalFields
		assert_same(target, fieldMap(:getGroup, m, 1, 453, target))
		assert_ended(view)
		assert_equal('A', target.getField(448))
		owner = party('P', 'P1')
		target, view = holding(NoPartySubIDs.new)
		assert_raise(Quickfix::FieldNotFound) { owner.getGroup(9, target) }
		view.totalFields
		owner.getGroup(1, target)
		assert_ended(view)
		assert_equal('P1', target.getField(523))
		# Message's, Header's and Trailer's clear the target before they look, so it loses its groups either way.
		[[->(m) { m }, 453, -> { NoPartyIDs.new }], [->(m) { m.getHeader }, 627, -> { hop('X') }],
		 [->(m) { m.getTrailer }, 627, -> { hop('X') }]].each do |part, tag, make|
			m = message
			[9, 1].each do |number|
				target, view = holding(make.())
				if number == 9
					assert_raise(Quickfix::FieldNotFound) { part.(m).getGroup(number, target) }
				else
					part.(m).getGroup(number, target)
				end
				assert_ended(view, "#{tag}, #{number}")
			end
		end
	end

	def test_swap
		# The bodies' groups pass to the other message.
		m, n = message, message
		v, w = views(m), views(n)
		m.swap(n)
		assert_views(v, BODY)
		assert_views(w, BODY)
	end

	def test_reverse_route
		# It removes the routing fields from the header, and a group at one of their tags goes with it.
		m = message
		v = views(m)
		m.getHeader.addGroup(hop('R', 128))
		routed = m.getHeader.getGroupRef(1, 128)
		reply = message
		w = views(reply)
		m.reverseRoute(reply.getHeader)
		assert_views(v, [])
		assert_ended(routed)
		assert_views(w, [], "its argument's")
	end

	def test_send
		# send removes PossDupFlag and OrigSendingTime from the header, and a group at one of their tags goes with
		# them. An initiator that is never started makes the session.
		settings = Quickfix::SessionSettings.new
		settings.setFromString("[DEFAULT]\nConnectionType=initiator\nSocketConnectHost=127.0.0.1\nSocketConnectPort=1\n" \
			"StartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\nHeartBtInt=30\n" \
			"[SESSION]\nBeginString=FIX.4.4\nSenderCompID=VIEWS\nTargetCompID=T\n")
		initiator = Quickfix::SocketInitiator.new(App.new, Quickfix::MemoryStoreFactory.new, settings)
		sessionID = Quickfix::SessionID.new('FIX.4.4', 'VIEWS', 'T')
		[['sendToTarget', ->(m) { Quickfix::Session.sendToTarget(m, sessionID) }],
		 ['send', ->(m) { Quickfix::Session.lookupSession(sessionID).send(m) }]].each do |name, send|
			m = message
			v = views(m)
			m.getHeader.addGroup(hop('P', 43))
			m.getHeader.addGroup(hop('O', 122))
			removed = [m.getHeader.getGroupRef(1, 43), m.getHeader.getGroupRef(1, 122)]
			send.(m)
			assert_views(v, [], name)
			removed.each { |view| assert_ended(view, name) }
		end
		# No such session: nothing is sent, and nothing goes.
		m = message
		v = views(m)
		m.getHeader.addGroup(hop('P', 43))
		kept = m.getHeader.getGroupRef(1, 43)
		assert_raise(Quickfix::SessionNotFound) do
			Quickfix::Session.sendToTarget(m, Quickfix::SessionID.new('FIX.4.4', 'NOBODY', 'T'))
		end
		assert_views(v, [])
		assert_equal('P', kept.getField(628))
	ensure
		assert(Quickfix._destroyInitiator(initiator)) if initiator
	end

	def test_every_method_is_classified
		DESTROYS.each do |klass, destroys|
			public = klass.public_instance_methods(false) + klass.singleton_methods(false)
			assert_equal([], destroys & KEEPS[klass], klass.name)
			assert_equal((destroys + KEEPS[klass]).sort, public.sort, klass.name)
		end
	end

	def test_the_registry_keeps_only_what_is_held
		m = message
		GC.start(full_mark: true, immediate_sweep: true)
		before = Quickfix._viewCount
		views = Array.new(200) { m.getGroupRef(1, 453) }
		assert_equal(before + 200, Quickfix._viewCount)
		views = Array.new(10) { m.getGroupRef(1, 453) }
		m.clear
		views.each { |view| assert_ended(view) }
		views = nil
		GC.start(full_mark: true, immediate_sweep: true)
		assert_equal(before, Quickfix._viewCount)
	end

	# A loop that ends a view on every pass leaves its ended views in the registry's map only until the map is
	# replaced without them, not until Ruby collects them: with GC off, the map stays small. It is replaced once its
	# ended views outnumber twice the views not ended, plus 16, and views that earlier tests dropped count as not
	# ended until they are collected, so the bound is relative to them.
	def test_the_registry_drops_ended_views_without_a_gc
		m, a = Quickfix44::NewOrderSingle.new, party('A', 'A1')
		GC.start(full_mark: true, immediate_sweep: true)
		GC.disable
		bound = 3 * (Quickfix._viewCount + 1) + 20
		assert_operator(bound, :<, 1000, 'below what keeping every ended view would hold')
		1000.times do
			m.clear
			m.addGroup(a)
			m.getGroupRef(1, 453)
		end
		assert_operator(Quickfix._viewEntries, :<=, bound)
	ensure
		GC.enable
	end

	# A Thread#raise waits for the thread holding the GVL to let go of it, which it does where it checks for
	# interrupts -- often in the registry's own calls into Ruby, where the raise then lands. Each must reach the
	# thread as itself, once the call is out of C++: none is lost, and none becomes another error.
	def test_a_thread_raise_reaches_the_thread_wherever_it_lands
		landed, raises = Quickfix._viewsLanded, 30
		received = 0
		worker = Thread.new do
			m = message
			Thread.handle_interrupt(Interrupted => :never) do
				while received < raises
					begin
						Thread.handle_interrupt(Interrupted => :immediate) { churn(m) }
					rescue Interrupted
						received += 1
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
		assert_operator(Quickfix._viewsLanded, :>, landed, 'no raise landed in the registry: the test proves nothing')
	ensure
		worker&.kill
	end

	def test_a_timeout_reaches_the_thread_wherever_it_lands
		within(120) do
			m = message
			10.times { assert_raise(Timeout::Error) { Timeout.timeout(0.01) { churn(m) } } }
		end
	end

	# A trap handler runs where the main thread checks for interrupts, often in the registry's own calls into Ruby,
	# and what it throws must reach its catch: none is lost. It throws only once armed, and disarms as it throws, so
	# every throw has a catch to reach.
	def test_a_throw_reaches_its_catch_wherever_it_lands
		landed, rounds = Quickfix._viewsLanded, 30
		armed, thrown, caught = false, 0, 0
		previous = Signal.trap('USR1') do
			if armed
				armed = false
				thrown += 1
				throw :stop
			end
		end
		sender = Thread.new do
			loop do
				Process.kill('USR1', Process.pid)
				sleep 0.005
			end
		end
		m = message
		Timeout.timeout(120) do
			while caught < rounds
				catch(:stop) do
					armed = true
					churn(m)
				end
				caught += 1
			end
		end
		assert_equal(thrown, caught)
		assert_operator(Quickfix._viewsLanded, :>, landed, 'no throw landed in the registry: the test proves nothing')
	ensure
		armed = false
		sender&.kill
		sender&.join
		# Signals sent before the sender stopped are handled here, disarmed, before the handler goes.
		sleep 0.1
		Signal.trap('USR1', previous || 'DEFAULT')
	end

	def test_a_kill_reaches_the_thread_wherever_it_lands
		landed = Quickfix._viewsLanded
		20.times do
			worker = Thread.new { churn(message) }
			sleep 0.01
			worker.kill
			assert_not_nil(worker.join(60), 'the thread ended')
			# false: it ended as a killed thread does, not by an exception.
			assert_equal(false, worker.status)
		end
		assert_operator(Quickfix._viewsLanded, :>, landed, 'no kill landed in the registry: the test proves nothing')
	end
end
