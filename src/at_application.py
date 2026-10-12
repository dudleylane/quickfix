import quickfix as fix

# What src/at_application.h does, through the Python binding: echo each
# NewOrderSingle and SecurityDefinition, and skip a resent order already seen.
class Application(fix.Application):
    def __init__(self):
        super().__init__()
        self.orderIDs = set()

    def onCreate(self, sessionID):
        self.reset()
        session = fix.Session.lookupSession( sessionID )
        if session is not None:
            session.reset()

    def onLogon(self, sessionID): return
    # Each definition logs on afresh, and its orders may reuse a ClOrdID from
    # the one before (#127).
    def onLogout(self, sessionID): self.reset()
    def toAdmin(self, message, sessionID): return

    # A FIXT counterparty's Logon names the application version it will use by
    # default, and the definitions expect it back on ours.
    def fromAdmin(self, message, sessionID):
        msgType = fix.MsgType()
        message.getHeader().getField( msgType )
        if( msgType.getValue() == fix.MsgType_Logon and message.isSetField( fix.DefaultApplVerID().getTag() ) ):
            defaultApplVerID = fix.DefaultApplVerID()
            message.getField( defaultApplVerID )
            fix.Session.lookupSession( sessionID ).setSenderDefaultApplVerID( defaultApplVerID.getValue() )

    def toApp(self, message, sessionID): return

    def fromApp(self, message, sessionID):
        msgType = fix.MsgType()
        message.getHeader().getField( msgType )
        if( msgType.getValue() == fix.MsgType_NewOrderSingle ):
            echo = fix.Message( message )
            possResend = fix.PossResend( False )
            if( message.getHeader().isSetField( possResend ) ):
                message.getHeader().getField( possResend )

            clOrdID = fix.ClOrdID()
            message.getField( clOrdID )

            # Per session, as at_application.h keys them.
            orderID = ( clOrdID.getString(), sessionID.toString() )
            if( possResend.getValue() ):
                if( orderID in self.orderIDs ):
                    return
            self.orderIDs.add( orderID )
            fix.Session.sendToTarget( echo, sessionID )
        elif( msgType.getValue() == fix.MsgType_Email ):
            echo = fix.Message( message )
            fix.Session.sendToTarget( echo, sessionID )
        elif( msgType.getValue() == fix.MsgType_SecurityDefinition ):
            echo = fix.Message( message )
            fix.Session.sendToTarget( echo, sessionID )
        else:
            raise fix.UnsupportedMessageType()

    def reset( self ):
        self.orderIDs = set()

