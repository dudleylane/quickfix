# What a transport hands out of itself -- a Session from lookupSession, registerSession or getSession, and its log --
# points into the transport, so the transport's destructor ends it first, and a constructor that raises ends those of
# the sessions it had made: using one then raises ReferenceError instead of reaching freed memory (#197). What a
# transport was made with is returned as the very object, and getSessionSettings as a copy. None of these transports
# is started, so none listens or connects.
import gc
import os
import unittest

import quickfix as fix

CERTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'bin', 'cfg', 'certs')
TRANSPORTS = (fix.SocketAcceptor, fix.SocketInitiator, fix.ThreadedSocketAcceptor, fix.ThreadedSocketInitiator,
              fix.SSLSocketAcceptor, fix.SSLSocketInitiator, fix.ThreadedSSLSocketAcceptor,
              fix.ThreadedSSLSocketInitiator)


def settings(transport, *senders):
    """Settings for transport, a class, with a session from each sender to TV."""
    acceptor = issubclass(transport, fix.Acceptor)
    ssl = 'SSL' in transport.__name__
    text = '[DEFAULT]\nStartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\n'
    if acceptor:
        text += 'ConnectionType=acceptor\nSocketAcceptPort=1\n'
        if ssl:
            text += (f'ServerCertificateFile={CERTS}/127_0_0_1_server.crt\n'
                     f'ServerCertificateKeyFile={CERTS}/127_0_0_1_server.key\n')
    else:
        text += 'ConnectionType=initiator\nSocketConnectHost=127.0.0.1\nSocketConnectPort=1\nHeartBtInt=30\n'
        if ssl:
            text += (f'CertificationAuthoritiesFile={CERTS}/certs/cacert.pem\n'
                     f'ClientCertificateFile={CERTS}/127_0_0_1_client.crt\n'
                     f'ClientCertificateKeyFile={CERTS}/127_0_0_1_client.key\n')
    for sender in senders:
        text += f'[SESSION]\nBeginString=FIX.4.2\nSenderCompID={sender}\nTargetCompID=TV\n'
    result = fix.SessionSettings()
    result.setFromString(text)
    return result


def sessionID(sender):
    return fix.SessionID('FIX.4.2', sender, 'TV')


class App(fix.Application):
    """With take, keeps a view of each session it is told of, from onCreate."""

    def __init__(self, take=False):
        super().__init__()
        self.take = take
        self.taken = []

    def onCreate(self, sessionID):
        if self.take:
            self.taken.append(fix.Session.lookupSession(sessionID))

    def onLogon(self, sessionID): pass
    def onLogout(self, sessionID): pass
    def toAdmin(self, message, sessionID): pass
    def fromAdmin(self, message, sessionID): pass
    def toApp(self, message, sessionID): pass
    def fromApp(self, message, sessionID): pass


class CountingLog(fix.Log):
    def __init__(self):
        super().__init__()
        self.events = []

    def clear(self): pass
    def backup(self): pass
    def onIncoming(self, s): pass
    def onOutgoing(self, s): pass
    def onEvent(self, s): self.events.append(s)


class CountingLogFactory(fix.LogFactory):
    def __init__(self):
        super().__init__()
        self.logs = []

    def create(self, *args):
        log = CountingLog()
        self.logs.append(log)
        return log

    def destroy(self, log): pass


def header(sender):
    """A message's text from sender to TV, which lookupSession's string overload reads the session from."""
    message = fix.Message()
    message.getHeader().setField(fix.BeginString('FIX.4.2'))
    message.getHeader().setField(fix.SenderCompID(sender))
    message.getHeader().setField(fix.TargetCompID('TV'))
    message.getHeader().setField(fix.MsgType('0'))
    return message.toString()


class TransportViewTestCase(unittest.TestCase):
    def assertEnded(self, view, msg=None):
        with self.assertRaises(ReferenceError, msg=msg):
            view.getSessionID() if isinstance(view, fix.Session) else view.onEvent('ended')

    def test_views_end_with_their_transport(self):
        for transport in TRANSPORTS:
            sender = 'V' + transport.__name__
            id = sessionID(sender)
            made = transport(App(), fix.MemoryStoreFactory(), settings(transport, sender))
            sessions = {'lookupSession': fix.Session.lookupSession(id),
                        'lookupSession(string)': fix.Session.lookupSession(header(sender), False),
                        'registerSession': fix.Session.registerSession(id),
                        'getSession': made.getSession(id)}
            log = made.getLog()
            for name, view in sessions.items():
                self.assertEqual(sender, view.getSessionID().getSenderCompID().getValue(), name)
            log.onEvent('alive')
            del made
            gc.collect()
            for name, view in sessions.items():
                self.assertEnded(view, f'{transport.__name__}: {name}')
            self.assertEnded(log, f'{transport.__name__}: getLog')

    def test_views_are_kept_until_their_transport_ends(self):
        before = fix._transportViewCount()
        made = fix.SocketInitiator(App(), fix.MemoryStoreFactory(), settings(fix.SocketInitiator, 'VKEPT'))
        views = [made.getSession(sessionID('VKEPT')), fix.Session.lookupSession(sessionID('VKEPT')), made.getLog()]
        self.assertEqual(before + len(views), fix._transportViewCount())
        del made
        gc.collect()
        self.assertEqual(before, fix._transportViewCount())

    def test_a_view_dropped_is_no_longer_kept(self):
        before = fix._transportViewCount()
        made = fix.SocketAcceptor(App(), fix.MemoryStoreFactory(), settings(fix.SocketAcceptor, 'VDROP'))
        view = made.getSession(sessionID('VDROP'))
        self.assertEqual(before + 1, fix._transportViewCount())
        del view
        self.assertEqual(before, fix._transportViewCount())
        del made

    def test_a_failed_constructor_ends_the_views_onCreate_took(self):
        for transport in TRANSPORTS:
            # VB exists already, so a transport with VA and VB makes VA, whose onCreate takes a view of it, and then
            # raises at VB, freeing VA.
            first = 'VB' + transport.__name__
            second = 'VA' + transport.__name__
            holder = transport(App(), fix.MemoryStoreFactory(), settings(transport, first))
            live = fix.Session.lookupSession(sessionID(first))
            app = App(take=True)
            with self.assertRaises(fix.ConfigError, msg=transport.__name__):
                transport(app, fix.MemoryStoreFactory(), settings(transport, second, first))
            self.assertEqual(1, len(app.taken), transport.__name__)
            self.assertEnded(app.taken[0], transport.__name__)
            self.assertIsNone(fix.Session.lookupSession(sessionID(second)), transport.__name__)
            # A view of a session that still exists is left alone.
            self.assertEqual(first, live.getSessionID().getSenderCompID().getValue(), transport.__name__)
            del holder
            gc.collect()
            self.assertEnded(live, transport.__name__)

    def test_a_script_log_is_returned_itself(self):
        # A Log a Python LogFactory made is the script's own object, which outlives the transport: not a view.
        factory = CountingLogFactory()
        made = fix.SocketAcceptor(App(), fix.MemoryStoreFactory(), settings(fix.SocketAcceptor, 'VLOG'), factory)
        log = made.getLog()
        self.assertTrue(any(log is created for created in factory.logs))
        del made
        gc.collect()
        self.assertNotEqual(0, int(log.this))
        log.onEvent('after')
        self.assertIn('after', log.events)

    def test_session_settings_are_a_copy(self):
        made = fix.SocketInitiator(App(), fix.MemoryStoreFactory(), settings(fix.SocketInitiator, 'VSET'))
        dictionary = made.getSessionSettings(sessionID('VSET'))
        self.assertTrue(dictionary.thisown)
        self.assertIsNone(made.getSessionSettings(sessionID('VNONE')))
        del made
        gc.collect()
        self.assertEqual('VSET', dictionary.getString('SenderCompID'))

    def test_what_a_transport_was_made_with_is_returned_itself(self):
        for transport in TRANSPORTS:
            sender = 'VW' + transport.__name__
            app, store = App(), fix.MemoryStoreFactory()
            made = transport(app, store, settings(transport, sender))
            self.assertIs(app, made.getApplication(), transport.__name__)
            self.assertIs(store, made.getMessageStoreFactory(), transport.__name__)
            del made

    def test_what_a_session_holds_of_its_transport_is_not_wrapped(self):
        # Each returned an opaque view of the session's state, freed with the transport.
        for name in ('getStore', 'getLog', 'getDataDictionaryProvider', 'getLogonTime', 'getAllowedRemoteAddresses'):
            self.assertFalse(hasattr(fix.Session, name), name)


if __name__ == '__main__':
    unittest.main()
