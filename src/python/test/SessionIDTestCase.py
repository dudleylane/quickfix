# A SessionID the binding hands Python -- a callback's argument, LogFactory.create's, Session.getSessionID()'s
# result -- is Python's own copy, never a view of the engine's: a session's ID is freed with its transport (#190,
# #193). Each test uses an initiator that is never started: making it makes its session, which calls onCreate and
# the LogFactory, and freeing it frees the session.
import gc
import unittest

import quickfix as fix


def settings(sender):
    result = fix.SessionSettings()
    result.setFromString('[DEFAULT]\nConnectionType=initiator\nSocketConnectHost=127.0.0.1\nSocketConnectPort=1\n'
                         'StartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\nHeartBtInt=30\n'
                         f'[SESSION]\nBeginString=FIX.4.2\nSenderCompID={sender}\nTargetCompID=ACC\n')
    return result


class Keeper(fix.Application):
    def __init__(self, change=False):
        super().__init__()
        self.kept = []
        self.change = change

    def onCreate(self, sessionID):
        self.kept.append(sessionID)
        if self.change:
            sessionID.fromString('FIX.4.2:CHANGED->BYAPP')

    def onLogon(self, sessionID): pass
    def onLogout(self, sessionID): pass
    def toAdmin(self, message, sessionID): pass
    def toApp(self, message, sessionID): pass
    def fromAdmin(self, message, sessionID): pass
    def fromApp(self, message, sessionID): pass


class NullLog(fix.Log):
    def clear(self): pass
    def backup(self): pass
    def onIncoming(self, s): pass
    def onOutgoing(self, s): pass
    def onEvent(self, s): pass


class KeepingLogFactory(fix.LogFactory):
    def __init__(self):
        super().__init__()
        self.kept = []
        self.logs = []

    def create(self, *args):
        self.kept.extend(args)
        log = NullLog()
        self.logs.append(log)
        return log

    def destroy(self, log): pass


class SessionIDTestCase(unittest.TestCase):

    def test_changing_a_handed_sessionID_leaves_the_sessions_own(self):
        # A view was the session's own ID: onCreate's change, and this one, changed the engine's.
        initiator = fix.SocketInitiator(Keeper(change=True), fix.MemoryStoreFactory(), settings('CHANGER'),
                                        KeepingLogFactory())
        session = fix.Session.lookupSession(fix.SessionID('FIX.4.2', 'CHANGER', 'ACC'))
        self.assertEqual('FIX.4.2:CHANGER->ACC', str(session.getSessionID()))
        session.getSessionID().fromString('FIX.4.2:CHANGED->BYAPP')
        self.assertEqual('FIX.4.2:CHANGER->ACC', str(session.getSessionID()))
        del session, initiator
        gc.collect()

    def test_a_handed_sessionID_outlives_its_transport(self):
        application, logFactory = Keeper(), KeepingLogFactory()
        initiator = fix.SocketInitiator(application, fix.MemoryStoreFactory(), settings('KEEPER'), logFactory)
        sessionID = fix.SessionID('FIX.4.2', 'KEEPER', 'ACC')
        kept = application.kept + logFactory.kept + [fix.Session.lookupSession(sessionID).getSessionID()]
        self.assertEqual(3, len(kept))
        self.assertTrue(all(handed.thisown for handed in kept))
        del initiator
        gc.collect()
        self.assertIsNone(fix.Session.lookupSession(sessionID))
        # Freed memory, while these were views.
        self.assertEqual(['FIX.4.2:KEEPER->ACC'] * 3, [str(handed) for handed in kept])


if __name__ == '__main__':
    unittest.main()
