# Run by TransportGcTestCase in a process of its own: a transport made with its store and log factories written
# inline, freed by the cycle collector in the same pass as them (#201). Before, the collector could free a factory
# first, and the transport's destructor, which destroys its sessions' stores and logs through them, used freed
# memory. It prints what it saw as key=value lines.
#   acceptor         a SocketAcceptor that refers to itself, then gc.collect()
#   initiator        the same with a SocketInitiator
#   stopped          a SocketAcceptor started and stopped first
#   scriptlogs       with a LogFactory written in Python, which keeps its logs (#205) and counts destroy's calls
#   appcycle         the Application refers to the transport: the application is not pinned (#204), so this is
#                    still collected, and the transport's sessions are unregistered
#   exit             left in its cycle for the interpreter's exit
#   traceback        held by a traceback kept until exit, as a failed assertion's is
#   scriptexit       exit with a LogFactory written in Python, whose destroy then runs during the interpreter's exit
#   scripttraceback  traceback, the same
#   factorycycle     the LogFactory, written in Python, refers to the transport: pinned, it keeps the transport,
#                    which is never collected, and its session stays registered
#   appfactorycycle  the same through the Application, which the LogFactory refers to
#   cycleexit        factorycycle, started and logged on to an initiator, and both left running for the exit
import gc
import socket
import sys
import time

import quickfix as fix

# Collected only where the script says, so that the counts it prints cannot change under it.
gc.disable()
destroyed = []


class App(fix.Application):
    def onCreate(self, sessionID): pass
    def onLogon(self, sessionID): pass
    def onLogout(self, sessionID): pass
    def toAdmin(self, message, sessionID): pass
    def fromAdmin(self, message, sessionID): pass
    def toApp(self, message, sessionID): pass
    def fromApp(self, message, sessionID): pass


class QuietLog(fix.Log):
    def clear(self): pass
    def backup(self): pass
    def onIncoming(self, text): pass
    def onOutgoing(self, text): pass
    def onEvent(self, text): pass


class KeepingLogFactory(fix.LogFactory):
    def __init__(self):
        super().__init__()
        self.logs = []

    def create(self, *args):
        log = QuietLog()
        self.logs.append(log)
        return log

    def destroy(self, log):
        destroyed.append(True)


def freePort():
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        return probe.getsockname()[1]


def settings(kind, port=1, sender='GC', target='TV'):
    result = fix.SessionSettings()
    connection = (f'ConnectionType=acceptor\nSocketAcceptPort={port}\n' if kind == 'acceptor' else
                  f'ConnectionType=initiator\nSocketConnectHost=127.0.0.1\nSocketConnectPort={port}\n'
                  'ReconnectInterval=1\n')
    result.setFromString('[DEFAULT]\nNonStopSession=Y\nUseDataDictionary=N\nHeartBtInt=30\n' + connection +
                         f'[SESSION]\nBeginString=FIX.4.2\nSenderCompID={sender}\nTargetCompID={target}\n')
    return result


def quiet():
    return fix.ScreenLogFactory(False, False, False)


def made(mode):
    if mode == 'initiator':
        return fix.SocketInitiator(App(), fix.MemoryStoreFactory(), settings('initiator'), quiet())
    if mode in ('scriptlogs', 'scriptexit', 'scripttraceback'):
        return fix.SocketAcceptor(App(), fix.MemoryStoreFactory(), settings('acceptor'), KeepingLogFactory())
    if mode == 'stopped':
        transport = fix.SocketAcceptor(App(), fix.MemoryStoreFactory(), settings('acceptor', freePort()), quiet())
        transport.start()
        transport.stop()
        return transport
    return fix.SocketAcceptor(App(), fix.MemoryStoreFactory(), settings('acceptor'), quiet())


def pins():
    return fix._quickfixPinCount() if hasattr(fix, '_quickfixPinCount') else 'none'


def registered():
    return fix.Session.lookupSession(fix.SessionID('FIX.4.2', 'GC', 'TV')) is not None


mode = sys.argv[1]
print(f'module={fix.__file__}', flush=True)
if mode == 'appcycle':
    app = App()
    transport = fix.SocketAcceptor(app, fix.MemoryStoreFactory(), settings('acceptor'), quiet())
    app.transport = transport
    del app, transport
elif mode in ('factorycycle', 'appfactorycycle'):
    app, logs = App(), KeepingLogFactory()
    transport = fix.SocketAcceptor(app, fix.MemoryStoreFactory(), settings('acceptor'), logs)
    if mode == 'factorycycle':
        logs.transport = transport
    else:
        logs.app, app.transport = app, transport
    del app, logs, transport
elif mode == 'cycleexit':
    port = freePort()
    logs = KeepingLogFactory()
    acceptor = fix.SocketAcceptor(App(), fix.MemoryStoreFactory(), settings('acceptor', port), logs)
    logs.transport = acceptor
    initiator = fix.SocketInitiator(App(), fix.MemoryStoreFactory(), settings('initiator', port, 'TV', 'GC'), quiet())
    acceptor.start()
    initiator.start()
    deadline = time.time() + 20
    while not (acceptor.isLoggedOn() and initiator.isLoggedOn()) and time.time() < deadline:
        time.sleep(0.05)
    print(f'loggedOn={acceptor.isLoggedOn() and initiator.isLoggedOn()}', flush=True)
    del logs, acceptor
elif mode in ('traceback', 'scripttraceback'):
    def fails():
        transport = made(mode)
        transport.me = transport
        raise AssertionError('held by this traceback')
    try:
        fails()
    except AssertionError:
        kept = sys.exc_info()
else:
    transport = made(mode)
    transport.me = transport
    if mode not in ('exit', 'scriptexit'):
        del transport
print(f'pinned={pins()}', flush=True)
if mode not in ('exit', 'traceback', 'scriptexit', 'scripttraceback', 'cycleexit'):
    gc.collect()
    print(f'collected=true', flush=True)
    print(f'pins={pins()}', flush=True)
    print(f'destroyed={len(destroyed)}', flush=True)
    print(f'registered={registered()}', flush=True)
print('end=true', flush=True)
