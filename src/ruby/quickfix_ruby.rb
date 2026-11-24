require 'quickfix'
require 'quickfix_fields'
require 'quickfix40'
require 'quickfix41'
require 'quickfix42'
require 'quickfix43'
require 'quickfix44'

module Quickfix
	class DataDictionary
		def getFieldName( field )
			name = String.new
			if( _getFieldName(field, name) )
				return name
			else
				return nil
			end
		end

		def getValueName( field, value )
			name = String.new
			if( _getValueName(field, value, name) )
				return name
			else
				return nil
			end
		end

		def getFieldTag( field )
			tag = 0
			return  _getFieldTag(field, tag)
		end

		def getGroup( msgType, group )
			delim = 0
			dictionary = Quickfix::DataDictionary.new
			delim = _getGroup( msgType, group, delim, dictionary )
			return nil if delim == nil
			return [delim, dictionary]
		end
	end

	# Equal by value, as the engine compares (#193): ==, eql? and hash by the
	# session's string, so two SessionIDs naming one session are equal, as Hash
	# keys too. Written here rather than in the wrapper, whose == would raise
	# TypeError for an object of another type, which Array#include? or a Hash
	# lookup can hand it.
	class SessionID
		def ==( other )
			other.is_a?( SessionID ) && to_s == other.to_s
		end
		alias eql? ==

		def hash
			to_s.hash
		end
	end

	class Initiator
		def start
			@quickfixThread = Thread.new { block() }
		end
	end

	class Acceptor
		def start
			@quickfixThread = Thread.new { block() }
		end
	end

	# What a transport hands out of itself -- a Session from lookupSession,
	# registerSession or getSession, and the transport's log -- dies with it
	# (#197; quickfix.i). Each such view keeps its transport, as @quickfixOwner,
	# so the GC, which frees a transport with no Ruby code running, never frees
	# one a view can reach. And each is kept here, by the address of what it
	# views, so that _destroyTransport ends the views of what a transport's
	# destructor frees, and a constructor that raised those of the sessions it
	# had made: from then on they raise ObjectPreviouslyDeleted. The maps are
	# weak, so a view the GC collects leaves them, and an owner too. All of it
	# runs here rather than in C++, where a raise or a kill landing in a call
	# into Ruby would skip C++ destructors.
	module TransportViews
		SESSIONS = ObjectSpace::WeakMap.new # a session's view => its address
		LOGS = ObjectSpace::WeakMap.new # a transport's log's view => its address
		OWNERS = ObjectSpace::WeakMap.new # a session's address => its transport

		# A view of a session (into SESSIONS) or a log (LOGS), owned by owner, or by
		# the session's transport if none is given. A proxy that owns its object,
		# such as a script's own Log, is not a view.
		def self.keep(view, owner, views)
			address = view && Quickfix._viewAddress(view)
			return view unless address
			owner ||= OWNERS[address]
			view.instance_variable_set(:@quickfixOwner, owner) if owner
			views[view] = address
			view
		end

		def self.endAt(addresses)
			doomed = addresses.to_h { |address| [address, true] }
			[SESSIONS, LOGS].each do |views|
				views.each { |view, address| Quickfix._endView(view) if doomed[address] }
			end
		end

		# A constructor that raised freed the sessions it had made, and onCreate
		# can have handed out a view of one.
		def self.endDangling
			live = Quickfix._liveSessions.to_h { |address| [address, true] }
			SESSIONS.each { |view, address| Quickfix._endView(view) unless live[address] }
		end

		# A transport made: its sessions' views keep it from now on, and so do the
		# ones onCreate handed out before it existed.
		def self.constructed(transport, sessions)
			mine = sessions.to_h { |address| [address, true] }
			sessions.each { |address| OWNERS[address] = transport }
			SESSIONS.each do |view, address|
				if mine[address] && !view.instance_variable_get(:@quickfixOwner)
					view.instance_variable_set(:@quickfixOwner, transport)
				end
			end
		end
	end

	# An initiator or acceptor holds C++ references to the application, store
	# factory, settings and log factory it was made with, so they must live as
	# long as it does: keep them on the Ruby object, whichever class made it
	# (#107). And its views (TransportViews): in ensure, so that a kill landing
	# once the transport exists still gives them their owner. A transport that
	# exists has a session; one whose constructor raised has none.
	module KeepsArguments
		def initialize(*args)
			super
		ensure
			sessions = Quickfix._sessionAddresses(self)
			if sessions.empty?
				TransportViews.endDangling
			else
				@quickfixArguments = args
				TransportViews.constructed(self, sessions)
			end
		end
	end

	constants.map { |name| const_get(name) }.each do |klass|
		next unless klass.is_a?(Class) && (klass < Initiator || klass < Acceptor)
		klass.prepend(KeepsArguments)
	end

	module TransportGetters
		def getSession(*args)
			TransportViews.keep(super, self, TransportViews::SESSIONS)
		end

		def getLog(*args)
			TransportViews.keep(super, self, TransportViews::LOGS)
		end

		# The store factory it was made with, which KeepsArguments keeps: SWIG
		# returned a second proxy of it, which owned nothing and kept nothing alive
		# (#197). getApplication returns the script's own object already (quickfix.i).
		def getMessageStoreFactory
			@quickfixArguments[1]
		end
	end
	[Initiator, Acceptor].each { |klass| klass.prepend(TransportGetters) }

	class << Session
		prepend(Module.new do
			def lookupSession(*args)
				TransportViews.keep(super, nil, TransportViews::SESSIONS)
			end

			def registerSession(*args)
				TransportViews.keep(super, nil, TransportViews::SESSIONS)
			end
		end)
	end

	# Deletes transport, as at exit, unless its block() is still running: false
	# then. The views of what its destructor frees are ended before it runs, and
	# again after, since a Ruby store or log factory runs inside it (#197).
	def self._destroyTransport(transport)
		initiator = transport.is_a?(Initiator)
		return false if Quickfix.send(initiator ? :_initiatorBlocking : :_acceptorBlocking, transport)
		addresses = Quickfix._transportAddresses(transport)
		sessions = Quickfix._sessionAddresses(transport)
		TransportViews.endAt(addresses)
		destroyed = Quickfix.send(initiator ? :_destroyInitiator : :_destroyAcceptor, transport)
		TransportViews.endAt(addresses)
		sessions.each do |address|
			TransportViews::OWNERS[address] = nil if TransportViews::OWNERS[address].equal?(transport)
		end
		destroyed
	end

	# The threaded transports' callbacks run on the "quickfix callbacks" thread
	# while the connection thread that made them waits (#104), and stop joins the
	# connection threads. Called from a callback, stop would join the thread
	# waiting for that very callback and never return (#111). C++ and Python
	# callbacks run on the connection thread itself, where stop returns after its
	# own wait times out (10-14 s); here it would never return, so refuse it.
	module StopsOutsideCallbacks
		def stop(*args)
			if Thread.current.equal?(Quickfix._callbackThread)
				raise ThreadError, "#{self.class}#stop cannot be called from a callback: it would wait for the " \
					"callback to return. Stop it from another thread, e.g. Thread.new { transport.stop }"
			end
			super
		end
	end

	[ThreadedSocketInitiatorBase, ThreadedSocketAcceptorBase,
	 ThreadedSSLSocketInitiatorBase, ThreadedSSLSocketAcceptorBase].each { |klass| klass.prepend(StopsOutsideCallbacks) }

	# At exit Ruby frees every object in no particular order, and an initiator or
	# acceptor freed after its store factory, log factory or application used them
	# from its destructor, which crashed the process (#105). at_exit runs before
	# that, while they are all alive: stop each one, wait for the thread #start
	# made and for any block() still running, and delete it.
	at_exit do
		[[Initiator, :_initiatorBlocking], [Acceptor, :_acceptorBlocking]].each do |kind, blocking|
			ObjectSpace.each_object(kind).to_a.each do |transport|
				begin
					transport.stop
					thread = transport.instance_variable_get(:@quickfixThread)
					# A start that failed ended that thread with its error, and join
					# raises it again; it must not skip the delete below (#123).
					begin
						thread.join(15) if thread
					rescue Exception
					end
					deadline = Time.now + 15
					sleep 0.05 while Quickfix.send(blocking, transport) && Time.now < deadline
					Quickfix._destroyTransport(transport)
				rescue StandardError
				end
			end
		end
	end

	# The plain names Python has for these too (#139). Every transport keeps its
	# arguments through KeepsArguments above, so they add nothing else.
	class ThreadedSocketInitiator < ThreadedSocketInitiatorBase; end
	class ThreadedSocketAcceptor < ThreadedSocketAcceptorBase; end
	class SSLSocketInitiator < SSLSocketInitiatorBase; end
	class SSLSocketAcceptor < SSLSocketAcceptorBase; end
	class ThreadedSSLSocketInitiator < ThreadedSSLSocketInitiatorBase; end
	class ThreadedSSLSocketAcceptor < ThreadedSSLSocketAcceptorBase; end

	class SocketInitiator < SocketInitiatorBase
		def initialize( application, storeFactory, settings, logFactory = nil )
			if( logFactory == nil )
				super( application, storeFactory, settings )
			else
				super( application, storeFactory, settings, logFactory )
			end
			@application = application;
			@storeFactory = storeFactory;
			@settings = settings;
			@logFactory = logFactory
		end
	end

	class SocketAcceptor < SocketAcceptorBase
		def initialize( application, storeFactory, settings, logFactory = nil )
			if( logFactory == nil )
				super( application, storeFactory, settings )
			else
				super( application, storeFactory, settings, logFactory )
			end
			@application = application;
			@storeFactory = storeFactory;
			@settings = settings;
			@logFactory = logFactory
		end
	end
end