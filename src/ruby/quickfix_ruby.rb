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

	# An initiator or acceptor holds C++ references to the application, store
	# factory, settings and log factory it was made with, so they must live as
	# long as it does: keep them on the Ruby object, whichever class made it
	# (#107).
	module KeepsArguments
		def initialize(*args)
			super
			@quickfixArguments = args
		end
	end

	constants.map { |name| const_get(name) }.each do |klass|
		next unless klass.is_a?(Class) && (klass < Initiator || klass < Acceptor)
		klass.prepend(KeepsArguments)
	end

	# At exit Ruby frees every object in no particular order, and an initiator or
	# acceptor freed after its store factory, log factory or application used them
	# from its destructor, which crashed the process (#105). at_exit runs before
	# that, while they are all alive: stop each one, wait for the thread #start
	# made and for any block() still running, and delete it.
	at_exit do
		[[Initiator, :_initiatorBlocking, :_destroyInitiator],
		 [Acceptor, :_acceptorBlocking, :_destroyAcceptor]].each do |kind, blocking, destroy|
			ObjectSpace.each_object(kind).to_a.each do |transport|
				begin
					transport.stop
					thread = transport.instance_variable_get(:@quickfixThread)
					thread.join(15) if thread
					deadline = Time.now + 15
					sleep 0.05 while Quickfix.send(blocking, transport) && Time.now < deadline
					Quickfix.send(destroy, transport)
				rescue StandardError
				end
			end
		end
	end

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