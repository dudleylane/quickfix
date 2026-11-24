require 'quickfix_ruby'
require 'test/unit'
require 'weakref'

# What a transport hands out of itself -- a Session from lookupSession, registerSession or getSession, and its log --
# points into the transport. Each such view holds its transport, so the GC never frees one a view can reach; deleting
# the transport, as at exit, ends its views first; and a constructor that raises ends those of the sessions it had
# made: using one then raises ObjectPreviouslyDeleted instead of reaching freed memory (#197). What a transport was
# made with is returned as the very object, and getSessionSettings as a copy. None of these transports is started,
# so none listens or connects.
class TransportViewTestCase < Test::Unit::TestCase
	CERTS = File.expand_path('../../../bin/cfg/certs', __dir__)
	TRANSPORTS = [Quickfix::SocketAcceptor, Quickfix::SocketInitiator, Quickfix::ThreadedSocketAcceptor,
	              Quickfix::ThreadedSocketInitiator, Quickfix::SSLSocketAcceptor, Quickfix::SSLSocketInitiator,
	              Quickfix::ThreadedSSLSocketAcceptor, Quickfix::ThreadedSSLSocketInitiator]

	# With take, keeps a view of each session it is told of, from onCreate.
	class App < Quickfix::Application
		attr_reader :taken

		def initialize(take = false)
			super()
			@take = take
			@taken = []
		end

		def onCreate(sessionID)
			@taken << Quickfix::Session.lookupSession(sessionID) if @take
		end

		def onLogon(sessionID); end
		def onLogout(sessionID); end
		def toAdmin(message, sessionID); end
		def fromAdmin(message, sessionID); end
		def toApp(message, sessionID); end
		def fromApp(message, sessionID); end
	end

	class CountingLog < Quickfix::Log
		attr_reader :events

		def initialize; super(); @events = []; end
		def clear; end
		def backup; end
		def onIncoming(s); end
		def onOutgoing(s); end
		def onEvent(s); @events << s; end
	end

	class CountingLogFactory < Quickfix::LogFactory
		attr_reader :logs

		def initialize; super; @logs = []; end
		def create(*args); log = CountingLog.new; @logs << log; log; end
		def destroy(log); end
	end

	def short(transport)
		transport.name.split('::').last
	end

	# Settings for transport, a class, with a session from each sender to TV.
	def settings(transport, *senders)
		ssl = short(transport).include?('SSL')
		text = "[DEFAULT]\nStartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\n"
		if transport < Quickfix::Acceptor
			text += "ConnectionType=acceptor\nSocketAcceptPort=1\n"
			text += "ServerCertificateFile=#{CERTS}/127_0_0_1_server.crt\n" \
				"ServerCertificateKeyFile=#{CERTS}/127_0_0_1_server.key\n" if ssl
		else
			text += "ConnectionType=initiator\nSocketConnectHost=127.0.0.1\nSocketConnectPort=1\nHeartBtInt=30\n"
			text += "CertificationAuthoritiesFile=#{CERTS}/certs/cacert.pem\n" \
				"ClientCertificateFile=#{CERTS}/127_0_0_1_client.crt\n" \
				"ClientCertificateKeyFile=#{CERTS}/127_0_0_1_client.key\n" if ssl
		end
		senders.each { |sender| text += "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=#{sender}\nTargetCompID=TV\n" }
		result = Quickfix::SessionSettings.new
		result.setFromString(text)
		result
	end

	def sessionID(sender)
		Quickfix::SessionID.new('FIX.4.2', sender, 'TV')
	end

	# A message's text from sender to TV, which lookupSession's string overload reads the session from.
	def header(sender)
		message = Quickfix::Message.new
		message.getHeader.setField(Quickfix::BeginString.new('FIX.4.2'))
		message.getHeader.setField(Quickfix::SenderCompID.new(sender))
		message.getHeader.setField(Quickfix::TargetCompID.new('TV'))
		message.getHeader.setField(Quickfix::MsgType.new('0'))
		message.toString
	end

	def owner(view)
		view.instance_variable_get(:@quickfixOwner)
	end

	def assert_ended(view, message = nil)
		error = assert_raise_kind_of(*[RuntimeError, message].compact) do
			view.is_a?(Quickfix::Session) ? view.getSessionID : view.onEvent('ended')
		end
		assert_equal('ObjectPreviouslyDeleted', error.class.name, message)
	end

	def test_views_end_with_their_transport
		TRANSPORTS.each do |transport|
			sender = "V#{short(transport)}"
			id = sessionID(sender)
			made = transport.new(App.new, Quickfix::MemoryStoreFactory.new, settings(transport, sender))
			sessions = { 'lookupSession' => Quickfix::Session.lookupSession(id),
			             'lookupSession(string)' => Quickfix::Session.lookupSession(header(sender), false),
			             'registerSession' => Quickfix::Session.registerSession(id),
			             'getSession' => made.getSession(id) }
			log = made.getLog
			sessions.each do |name, view|
				assert_equal(sender, view.getSessionID.getSenderCompID.getValue, "#{transport}: #{name}")
			end
			log.onEvent('alive')
			assert(Quickfix._destroyTransport(made), transport.to_s)
			sessions.each { |name, view| assert_ended(view, "#{transport}: #{name}") }
			assert_ended(log, "#{transport}: getLog")
		end
	end

	def test_views_hold_their_transport
		TRANSPORTS.each do |transport|
			sender = "VH#{short(transport)}"
			id = sessionID(sender)
			made = transport.new(App.new, Quickfix::MemoryStoreFactory.new, settings(transport, sender))
			{ 'lookupSession' => Quickfix::Session.lookupSession(id), 'registerSession' => Quickfix::Session.registerSession(id),
			  'getSession' => made.getSession(id), 'getLog' => made.getLog }.each do |name, view|
				assert(owner(view).equal?(made), "#{transport}: #{name}")
			end
			assert(Quickfix._destroyTransport(made), transport.to_s)
		end
	end

	def test_a_view_holds_its_transport
		# Made on a thread of its own, whose stack is gone once it ends, so that only the view can hold the transport.
		view, weak = Thread.new do
			made = Quickfix::SocketAcceptor.new(App.new, Quickfix::MemoryStoreFactory.new,
			                                    settings(Quickfix::SocketAcceptor, 'VHOLD'))
			[made.getSession(sessionID('VHOLD')), WeakRef.new(made)]
		end.value
		3.times { GC.start(full_mark: true, immediate_sweep: true) }
		assert(weak.weakref_alive?)
		assert_equal('VHOLD', view.getSessionID.getSenderCompID.getValue)
		assert(Quickfix._destroyTransport(weak.__getobj__))
		assert_ended(view)
	end

	def test_a_view_onCreate_took_gets_its_transport
		app = App.new(true)
		made = Quickfix::SocketInitiator.new(app, Quickfix::MemoryStoreFactory.new,
		                                     settings(Quickfix::SocketInitiator, 'VOWN'))
		assert_equal(1, app.taken.size)
		assert(owner(app.taken[0]).equal?(made))
		assert(Quickfix._destroyTransport(made))
		assert_ended(app.taken[0])
	end

	def test_a_failed_constructor_ends_the_views_onCreate_took
		TRANSPORTS.each do |transport|
			# VB exists already, so a transport with VA and VB makes VA, whose onCreate takes a view of it, and then
			# raises at VB, freeing VA.
			first = "VB#{short(transport)}"
			second = "VA#{short(transport)}"
			holder = transport.new(App.new, Quickfix::MemoryStoreFactory.new, settings(transport, first))
			live = Quickfix::Session.lookupSession(sessionID(first))
			app = App.new(true)
			assert_raise(Quickfix::ConfigError, transport.to_s) do
				transport.new(app, Quickfix::MemoryStoreFactory.new, settings(transport, second, first))
			end
			assert_equal(1, app.taken.size, transport.to_s)
			assert_nil(owner(app.taken[0]), transport.to_s)
			assert_ended(app.taken[0], transport.to_s)
			assert_nil(Quickfix::Session.lookupSession(sessionID(second)), transport.to_s)
			# A view of a session that still exists is left alone.
			assert_equal(first, live.getSessionID.getSenderCompID.getValue, transport.to_s)
			assert(Quickfix._destroyTransport(holder), transport.to_s)
			assert_ended(live, transport.to_s)
		end
	end

	def test_a_script_log_is_reached_through_a_view
		# The engine's Log for a Ruby one is an adapter, which the transport frees: getLog's view of it ends, and the
		# script's own Log is untouched.
		factory = CountingLogFactory.new
		made = Quickfix::SocketAcceptor.new(App.new, Quickfix::MemoryStoreFactory.new,
		                                    settings(Quickfix::SocketAcceptor, 'VLOG'), factory)
		log = made.getLog
		log.onEvent('through the view')
		assert(factory.logs.any? { |created| created.events.include?('through the view') })
		assert(owner(log).equal?(made))
		assert(Quickfix._destroyTransport(made))
		assert_ended(log)
		factory.logs.each { |created| created.onEvent('after') }
	end

	def test_session_settings_are_a_copy
		made = Quickfix::SocketInitiator.new(App.new, Quickfix::MemoryStoreFactory.new,
		                                     settings(Quickfix::SocketInitiator, 'VSET'))
		dictionary = made.getSessionSettings(sessionID('VSET'))
		assert_nil(made.getSessionSettings(sessionID('VNONE')))
		assert(Quickfix._destroyTransport(made))
		assert_equal('VSET', dictionary.getString('SenderCompID'))
	end

	def test_what_a_transport_was_made_with_is_returned_itself
		TRANSPORTS.each do |transport|
			app = App.new
			store = Quickfix::MemoryStoreFactory.new
			made = transport.new(app, store, settings(transport, "VW#{short(transport)}"))
			assert(made.getApplication.equal?(app), transport.to_s)
			assert(made.getMessageStoreFactory.equal?(store), transport.to_s)
			assert(Quickfix._destroyTransport(made), transport.to_s)
		end
	end

	def test_what_a_session_holds_of_its_transport_is_not_wrapped
		# Each returned an opaque view of the session's state, freed with the transport.
		[:getStore, :getLog, :getDataDictionaryProvider, :getLogonTime, :getAllowedRemoteAddresses].each do |name|
			assert(!Quickfix::Session.method_defined?(name), name.to_s)
		end
	end
end
