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

	# start runs block() on a new thread, which clears m_stop (marking the transport
	# started) only once it has entered block(). stop() does nothing while the
	# transport is still marked stopped, so a stop() right after start() would miss
	# it and leave block() running until exit (#206). Wait until block() has entered,
	# or its thread has ended because block() raised -- a failed start still surfaces
	# when the returned thread is joined (#123). m_stop is atomic, so isStopped reads
	# it without a race.
	class Initiator
		def start
			@quickfixThread = Thread.new { block() }
			sleep 0.0001 while isStopped && @quickfixThread.alive?
			@quickfixThread
		end
	end

	class Acceptor
		def start
			@quickfixThread = Thread.new { block() }
			sleep 0.0001 while isStopped && @quickfixThread.alive?
			@quickfixThread
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
	# (#107). Kept there, they still went in the same GC as the transport, in any
	# order, and its destructor used its store and log factories (#201): those
	# are pinned from C++ too, until its C++ object is deleted. And its views
	# (TransportViews): in ensure, so that a kill landing once the transport
	# exists still gives them their owner. A transport that exists has a
	# session; one whose constructor raised has none.
	module KeepsArguments
		def initialize(*args)
			@quickfixCloseMutex = Thread::Mutex.new # serialises #close (#207)
			super
		ensure
			sessions = Quickfix._sessionAddresses(self)
			if sessions.empty?
				TransportViews.endDangling
			else
				@quickfixArguments = args
				Quickfix.send(is_a?(Initiator) ? :_quickfixPinInitiator : :_quickfixPinAcceptor, self, args[1], args[3])
				TransportViews.constructed(self, sessions)
			end
		end
	end

	constants.map { |name| const_get(name) }.each do |klass|
		next unless klass.is_a?(Class) && (klass < Initiator || klass < Acceptor)
		klass.prepend(KeepsArguments)
	end

	# SynchronizedApplication holds a C++ reference to the application it wraps but
	# kept nothing referring to the script's object, so once the script dropped its
	# own reference the GC freed the application and the next callback through the
	# wrapper used freed memory (#208). Keep it on the wrapper's own object, the way
	# KeepsArguments keeps a transport's arguments.
	module KeepsApplication
		def initialize(application, *args)
			super
			@quickfixApplication = application
		end
	end
	SynchronizedApplication.prepend(KeepsApplication)

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

	# Stops transport, waits up to timeout seconds for the thread #start made and for block() to
	# finish, then deletes it (_destroyTransport: unregisters its sessions, lets its factories go
	# (#201), ends its views (#197)). Shared by #close and the at_exit hook. Returns :destroyed; or
	# :running if it was still blocking at the deadline or _destroyTransport refused the delete, so it
	# was not deleted; or the Exception that joining the thread raised if start had failed (#123) --
	# after the delete, so a caller can report it. Does not raise; #close turns these into its contract.
	def self._closeTransport(transport, timeout = 15)
		transport.stop
		thread = transport.instance_variable_get(:@quickfixThread)
		failure = nil
		begin
			thread.join(timeout) if thread
		rescue Exception => error
			failure = error
		end
		blocking = transport.is_a?(Initiator) ? :_initiatorBlocking : :_acceptorBlocking
		deadline = Time.now + timeout
		sleep 0.05 while Quickfix.send(blocking, transport) && Time.now < deadline
		return :running if Quickfix.send(blocking, transport)
		# _destroyTransport refuses (returns false) if the transport is blocking again or its pointer is
		# already gone, so it was not deleted here: report :running rather than let #close mark a live
		# transport closed, which would make the at_exit hook skip it and free it out of order (#105).
		return :running unless Quickfix._destroyTransport(transport)
		failure || :destroyed
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

	# A script that keeps a transport reachable from its factory or application -- the #201 pin keeps the
	# transport while such a reference stands -- can end it deterministically with #close, rather than wait
	# for the GC or the at_exit hook (#207). close stops the transport, waits for the thread #start made and
	# for block() to finish, then deletes it: its sessions are unregistered, its factories let go, its views
	# ended. It raises if called from inside an engine callback -- it would delete the transport under the
	# callback's own stack, as stop refuses a threaded callback (#111) -- and if the transport is still
	# running after timeout seconds. close is the delete, so, as when a transport is destroyed in C++, it
	# must not race another thread's call on the same transport (block() excepted, which close is designed to
	# end): a script driving poll() ends its poll loop before closing from another thread, or closes from the
	# poll thread itself. Idempotent; afterward the proxy raises ObjectPreviouslyDeleted, as a deleted view
	# does (#197).
	module Closeable
		def close(timeout = 15)
			# Refuse from inside any callback, and before taking the lock. Quickfix._inCallback counts the
			# engine callbacks running on this thread (the binding bumps it around every one), so this
			# catches close() reached through onLogon, fromApp, a toApp sent via Session.sendToTarget, or a
			# poll() the script drives -- each of which would delete the transport under the callback's own
			# stack, a use-after-free. Checking before the mutex also means a callback that reaches close()
			# while another thread holds it raises rather than blocking on it, which on a threaded transport
			# is the connection thread this callback's stop() would then wait for (#111). owned? refuses a
			# reentrant close() on the thread already in one -- the non-reentrant mutex would deadlock it.
			if Quickfix._inCallback || @quickfixCloseMutex.owned?
				raise ThreadError, "#{self.class}#close cannot be called from inside an engine callback: it " \
					"would delete the transport under the callback's own stack. Close it from another thread."
			end
			@quickfixCloseMutex.synchronize do
				return if @quickfixClosed
				result = Quickfix._closeTransport(self, timeout)
				raise "#{self.class}#close: the transport is still running after #{timeout} s" if result == :running
				@quickfixClosed = true
				raise result if result.is_a?(Exception) # start had failed (#123); deleted first, reported now
			end
			nil
		end
	end
	[Initiator, Acceptor].each { |klass| klass.prepend(Closeable) }

	# At exit Ruby frees every object in no particular order, and an initiator or
	# acceptor freed after its store factory, log factory or application used them
	# from its destructor, which crashed the process (#105). at_exit runs before
	# that, while they are all alive: close each one not already closed, through the
	# same _closeTransport #close uses -- but swallowing a failed start's error
	# (#123), which #close reports and a script can override, and which must not stop
	# the exit hook from closing the rest.
	at_exit do
		[Initiator, Acceptor].each do |kind|
			ObjectSpace.each_object(kind).to_a.each do |transport|
				begin
					Quickfix._closeTransport(transport) unless transport.instance_variable_get(:@quickfixClosed)
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