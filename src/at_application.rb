require 'quickfix_ruby'

class Application < Quickfix::Application

	# Spelt initiatlize until #127, so it never ran.
	def initialize
		super
		@orderIDs = Hash.new
	end

	def onCreate(sessionID)
		reset()
		session = Quickfix::Session.lookupSession( sessionID )
		session.reset() if session != nil
	end

	def onLogon(sessionID)
	end

	def onLogout(sessionID)
		reset()
	end

	def toAdmin(message, sessionID)
	end

	# A FIXT counterparty's Logon names the application version it will use by
	# default, and the definitions expect it back on ours.
	def fromAdmin(message, sessionID)
		msgType = Quickfix::MsgType.new
		message.getHeader().getField( msgType )
		if( msgType.getValue() == Quickfix::MsgType_Logon() && message.isSetField( Quickfix::DefaultApplVerID.new.getTag() ) )
			defaultApplVerID = Quickfix::DefaultApplVerID.new
			message.getField( defaultApplVerID )
			Quickfix::Session.lookupSession( sessionID ).setSenderDefaultApplVerID( defaultApplVerID.getValue() )
		end
	end

	def toApp(message, sessionID)
	end

	def fromApp(message, sessionID)
		msgType = Quickfix::MsgType.new
		message.getHeader().getField( msgType )
		if( msgType.getValue() == Quickfix::MsgType_NewOrderSingle() )
			echo = Quickfix::Message.new( message )
			possResend = Quickfix::PossResend.new( false )
			if( message.getHeader().isSetField( possResend ) )
				message.getHeader().getField( possResend )
			end

			clOrdID = Quickfix::ClOrdID.new
			message.getField( clOrdID )

			if( possResend.getValue() )
				if( @orderIDs.has_key?([clOrdID.getString(), sessionID.toString()]) )
					return
				end
			end
			@orderIDs[[clOrdID.getString(), sessionID.toString()]] = [clOrdID.getString(), sessionID.toString()]
			Quickfix::Session::sendToTarget( echo, sessionID )
		elsif( msgType.getValue() == Quickfix::MsgType_Email() )
			echo = Quickfix::Message.new( message )
			Quickfix::Session::sendToTarget( echo, sessionID )
		elsif( msgType.getValue() == Quickfix::MsgType_SecurityDefinition() )
			echo = Quickfix::Message.new( message )
			Quickfix::Session::sendToTarget( echo, sessionID )
		else
			raise Quickfix::UnsupportedMessageType.new
		end
	end

	def reset
		@orderIDs = Hash.new
	end

end