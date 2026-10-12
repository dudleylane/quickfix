# Run by CallbackErrorTestCase in a process of its own: an acceptor and an
# initiator, where one of the acceptor's callbacks raises an exception the engine
# does not expect. The binding prints it and ends the process; it used to call
# Py_Exit, which finalized the interpreter under the reactor's running thread
# and crashed (#122). stdout is a pipe here, so it is block-buffered, and the
# test also sees whether Python's buffers were flushed on the way out.
import sys
import time

import quickfix as fix

dir, port, kind, where = sys.argv[1:5]
acceptorClass, initiatorClass = {
    'socket': (fix.SocketAcceptor, fix.SocketInitiator),
    'threaded': (fix.ThreadedSocketAcceptor, fix.ThreadedSocketInitiator),
}[kind]


class App(fix.Application):
    def __init__(self, raises=None):
        super().__init__()
        self.raises = raises

    def onCreate(self, sessionID): pass

    def onLogon(self, sessionID):
        if self.raises == 'onLogon':
            raise ValueError('raised in onLogon')
        if self.raises == 'exit':
            sys.exit(3)

    def onLogout(self, sessionID): pass
    def toAdmin(self, message, sessionID): pass
    def fromAdmin(self, message, sessionID): pass
    def toApp(self, message, sessionID): pass
    def fromApp(self, message, sessionID): pass


class RaisingLog(fix.Log):
    def clear(self): pass
    def backup(self): pass
    def onIncoming(self, s): raise ValueError('raised in Log.onIncoming')
    def onOutgoing(self, s): pass
    def onEvent(self, s): pass


class RaisingLogFactory(fix.LogFactory):
    def __init__(self):
        super().__init__()
        self.logs = []

    def create(self, *args):
        log = RaisingLog()
        self.logs.append(log)
        return log

    def destroy(self, log): pass


def settings(name, body):
    path = f'{dir}/{name}.cfg'
    with open(path, 'w') as f:
        f.write('[DEFAULT]\nStartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\n' + body)
    return fix.SessionSettings(path)


acceptorSettings = settings('acceptor', f'ConnectionType=acceptor\nSocketAcceptPort={port}\n'
                            '[SESSION]\nBeginString=FIX.4.2\nSenderCompID=ACC\nTargetCompID=INI\n')
if where == 'log':
    acceptor = acceptorClass(App(), fix.MemoryStoreFactory(), acceptorSettings, RaisingLogFactory())
else:
    acceptor = acceptorClass(App(where), fix.MemoryStoreFactory(), acceptorSettings)
# 'initiator': the initiator's onLogon raises instead -- on the reactor that
# crashed most often, in 5 of 8 runs.
initiator = initiatorClass(App('onLogon' if where == 'initiator' else None), fix.MemoryStoreFactory(),
                           settings('initiator', 'ConnectionType=initiator\nSocketConnectHost=127.0.0.1\n'
                                    f'SocketConnectPort={port}\nReconnectInterval=1\nHeartBtInt=30\n'
                                    '[SESSION]\nBeginString=FIX.4.2\nSenderCompID=INI\nTargetCompID=ACC\n'))

print('buffered=true')
acceptor.start()
initiator.start()
time.sleep(20)
print('survived=true', flush=True)
