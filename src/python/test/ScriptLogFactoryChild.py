# Run by ScriptLogFactoryTestCase in a process of its own. The engine holds each Log a LogFactory creates until it
# calls destroy with it; the binding keeps the script's Log alive meanwhile and hands destroy the object create
# returned (#205).
#   drop:  a LogFactory that keeps no reference to the Log it returns. After the logs are collected, events keep
#          reaching them -- on the unfixed binding the collected Log's director is used and the process crashes.
#   match: a LogFactory that keeps each created Log keyed by id() and drops it in destroy by the same id. After the
#          transport is deleted it must hold 0 (destroy was handed the create object); unfixed, destroy gets a
#          different proxy, so the pop misses and every log is still held.
import gc
import socket
import sys
import time
import weakref

import quickfix as fix

gc.disable()


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


class DroppingLogFactory(fix.LogFactory):
    def create(self, *args):
        return QuietLog()           # keeps no reference

    def destroy(self, log): pass


class MatchingLogFactory(fix.LogFactory):
    def __init__(self):
        super().__init__()
        self.held = {}

    def create(self, *args):
        log = QuietLog()
        self.held[id(log)] = log
        return log

    def destroy(self, log):
        self.held.pop(id(log), None)   # by the create object's identity


class WeakLogFactory(fix.LogFactory):
    # Keeps only weak references to its logs, so they live exactly as long as the binding keeps them.
    def __init__(self):
        super().__init__()
        self.refs = []

    def create(self, *args):
        log = QuietLog()
        self.refs.append(weakref.ref(log))
        return log

    def destroy(self, log): pass


class SharedLogFactory(fix.LogFactory):
    # Returns one Log for every create: the binding keeps it keyed by its C++ address with a count, released only
    # once destroy has been called that many times.
    def __init__(self):
        super().__init__()
        self.log = QuietLog()
        self.creates = 0
        self.destroys = 0

    def create(self, *args):
        self.creates += 1
        return self.log

    def destroy(self, log):
        if log is self.log:        # identity: did destroy get the shared log?
            self.destroys += 1


def binding_held(factory):
    return len(getattr(factory, '_quickfixLogs', {}))   # the binding's own keep map on the factory


def freePort():
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        return probe.getsockname()[1]


def settings(kind, port, sender, target):
    result = fix.SessionSettings()
    connection = (f'ConnectionType=acceptor\nSocketAcceptPort={port}\n' if kind == 'acceptor' else
                  f'ConnectionType=initiator\nSocketConnectHost=127.0.0.1\nSocketConnectPort={port}\nReconnectInterval=1\n')
    result.setFromString('[DEFAULT]\nNonStopSession=Y\nUseDataDictionary=N\nHeartBtInt=30\n' + connection +
                         f'[SESSION]\nBeginString=FIX.4.2\nSenderCompID={sender}\nTargetCompID={target}\n')
    return result


def quiet():
    return fix.ScreenLogFactory(False, False, False)


mode = sys.argv[1]
print(f'module={fix.__file__}', flush=True)
port = freePort()

if mode == 'drop':
    acceptor = fix.SocketAcceptor(App(), fix.MemoryStoreFactory(), settings('acceptor', port, 'ACC', 'CLI'),
                                  DroppingLogFactory())
    initiator = fix.SocketInitiator(App(), fix.MemoryStoreFactory(), settings('initiator', port, 'CLI', 'ACC'), quiet())
    acceptor.start()
    initiator.start()
    deadline = time.time() + 20
    while not acceptor.isLoggedOn() and time.time() < deadline:
        time.sleep(0.05)
    print(f'loggedOn={acceptor.isLoggedOn()}', flush=True)
    for _ in range(5):
        gc.collect()                 # frees the dropped logs on the unfixed binding
    # Drive events to the acceptor's logs after the collection: a message in, and heartbeats over the wait.
    fix.Session.sendToTarget(fix.Message(), fix.SessionID('FIX.4.2', 'CLI', 'ACC'))
    time.sleep(3)
    print('survived=true', flush=True)

elif mode == 'match':
    factory = MatchingLogFactory()
    acceptor = fix.SocketAcceptor(App(), fix.MemoryStoreFactory(), settings('acceptor', port, 'ACC', 'CLI'), factory)
    created = len(factory.held)
    del acceptor
    gc.collect()                     # deletes the transport -> destroy for each log
    print(f'created={created}', flush=True)
    print(f'held_after_destroy={len(factory.held)}', flush=True)   # the script's dict: destroy got the create object
    print(f'binding_held={binding_held(factory)}', flush=True)     # the binding's keep map: release actually fired
    print('survived=true', flush=True)

elif mode == 'weakref':
    factory = WeakLogFactory()
    acceptor = fix.SocketAcceptor(App(), fix.MemoryStoreFactory(), settings('acceptor', port, 'ACC', 'CLI'), factory)
    gc.collect()
    alive_during = sum(1 for ref in factory.refs if ref() is not None)   # the binding keeps them
    del acceptor
    gc.collect()                     # the binding releases them; nothing else holds them
    alive_after = sum(1 for ref in factory.refs if ref() is not None)
    print(f'created={len(factory.refs)}', flush=True)
    print(f'alive_during={alive_during}', flush=True)                # == created
    print(f'alive_after={alive_after}', flush=True)                  # 0
    print(f'binding_held={binding_held(factory)}', flush=True)       # 0
    print('survived=true', flush=True)

elif mode == 'shared':
    factory = SharedLogFactory()
    acceptor = fix.SocketAcceptor(App(), fix.MemoryStoreFactory(), settings('acceptor', port, 'ACC', 'CLI'), factory)
    creates = factory.creates
    del acceptor
    gc.collect()
    print(f'creates={creates}', flush=True)                          # >= 2 (transport + session), the same object
    print(f'outstanding={factory.creates - factory.destroys}', flush=True)   # 0: destroy got the shared log
    print(f'binding_held={binding_held(factory)}', flush=True)       # 0: released after the last destroy
    print('survived=true', flush=True)
