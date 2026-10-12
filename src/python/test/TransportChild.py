# Run by TransportTestCase in a process of its own: an acceptor and an initiator
# started in one process, talking to each other, with a LogFactory and Log
# written in Python. It prints what it saw as key=value lines. With "stop" it
# stops both before it ends; with "exit" it leaves both running, for the
# interpreter's exit to deal with. Everything but the applications is passed as
# a temporary: the transport must keep what it was made with (#110).
import gc
import os
import sys
import threading
import time

import quickfix as fix
import quickfix42 as fix42

dir, port, kind, ending = sys.argv[1:5]
acceptorClass, initiatorClass = {
    'socket': (fix.SocketAcceptor, fix.SocketInitiator),
    'threaded': (fix.ThreadedSocketAcceptor, fix.ThreadedSocketInitiator),
    'base': (fix.SocketAcceptorBase, fix.SocketInitiatorBase),
    'threadedbase': (fix.ThreadedSocketAcceptorBase, fix.ThreadedSocketInitiatorBase),
    # TLS on both transports, with the example certificates (#139).
    'ssl': (fix.SSLSocketAcceptor, fix.SSLSocketInitiator),
    'threadedssl': (fix.ThreadedSSLSocketAcceptor, fix.ThreadedSSLSocketInitiator),
}[kind]
certs = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'bin', 'cfg', 'certs')
acceptorTls = (f'ServerCertificateFile={certs}/127_0_0_1_server.crt\n'
               f'ServerCertificateKeyFile={certs}/127_0_0_1_server.key\n') if 'ssl' in kind else ''
initiatorTls = (f'CertificationAuthoritiesFile={certs}/certs/cacert.pem\n'
                f'ClientCertificateFile={certs}/127_0_0_1_client.crt\n'
                f'ClientCertificateKeyFile={certs}/127_0_0_1_client.key\n') if 'ssl' in kind else ''


class App(fix.Application):
    def __init__(self):
        super().__init__()
        self.received = []
        self.refuse = False

    def onCreate(self, sessionID): pass
    def onLogon(self, sessionID): pass
    def onLogout(self, sessionID): pass
    def toAdmin(self, message, sessionID): pass
    def fromAdmin(self, message, sessionID): pass

    def toApp(self, message, sessionID):
        if self.refuse:
            raise fix.DoNotSend()

    def fromApp(self, message, sessionID):
        clOrdID = fix.ClOrdID()
        message.getField(clOrdID)
        self.received.append(clOrdID.getValue())


counts = {'incoming': 0, 'outgoing': 0, 'event': 0}


class CountingLog(fix.Log):
    def clear(self): pass
    def backup(self): pass
    def onIncoming(self, s): counts['incoming'] += 1
    def onOutgoing(self, s): counts['outgoing'] += 1
    def onEvent(self, s): counts['event'] += 1


# Each Log a Python LogFactory returns is handed to C++, which takes ownership
# of it under a SWIG director mutex; SWIG 4.3.0's freed that mutex on unlock,
# so the second one aborted with a double free (#109). Two transports with a
# session each make at least two.
class CountingLogFactory(fix.LogFactory):
    def __init__(self):
        super().__init__()
        self.logs = []

    def create(self, *args):
        log = CountingLog()
        self.logs.append(log)
        return log

    def destroy(self, log): pass


def settings(name, body):
    path = f'{dir}/{name}.cfg'
    with open(path, 'w') as f:
        f.write('[DEFAULT]\nStartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\n' + body)
    return fix.SessionSettings(path)


def waitFor(seconds, predicate):
    deadline = time.time() + seconds
    while not predicate() and time.time() < deadline:
        time.sleep(0.05)
    return predicate()


def order(id):
    message = fix42.NewOrderSingle()
    for field in [fix.ClOrdID(id), fix.HandlInst('1'), fix.Symbol('IBM'), fix.Side(fix.Side_BUY),
                  fix.TransactTime(), fix.OrdType(fix.OrdType_MARKET)]:
        message.setField(field)
    message.getHeader().setField(fix.SenderCompID('INI'))
    message.getHeader().setField(fix.TargetCompID('ACC'))
    return fix.Session.sendToTarget(message)


acceptorApp, initiatorApp = App(), App()
acceptor = acceptorClass(acceptorApp, fix.MemoryStoreFactory(),
                         settings('acceptor', f'ConnectionType=acceptor\nSocketAcceptPort={port}\n' + acceptorTls +
                                  '[SESSION]\nBeginString=FIX.4.2\nSenderCompID=ACC\nTargetCompID=INI\n'),
                         CountingLogFactory())
initiator = initiatorClass(initiatorApp, fix.MemoryStoreFactory(),
                           settings('initiator', 'ConnectionType=initiator\nSocketConnectHost=127.0.0.1\n' + initiatorTls +
                                    f'SocketConnectPort={port}\nReconnectInterval=1\nHeartBtInt=30\n'
                                    '[SESSION]\nBeginString=FIX.4.2\nSenderCompID=INI\nTargetCompID=ACC\n'),
                           CountingLogFactory())
gc.collect()
print('constructed=true', flush=True)

acceptor.start()
initiator.start()
print('started=true', flush=True)

ticks = 0


def tick():
    global ticks
    while True:
        ticks += 1
        time.sleep(0.01)


threading.Thread(target=tick, daemon=True).start()
print(f'loggedOn={waitFor(20, lambda: initiator.isLoggedOn() and acceptor.isLoggedOn())}', flush=True)

order('sent')
initiatorApp.refuse = True
print(f'refused={not order("refused")}', flush=True)
initiatorApp.refuse = False
order('after')
waitFor(10, lambda: 'after' in acceptorApp.received)
print(f'received={",".join(acceptorApp.received)}', flush=True)
print(f'logged={all(count > 0 for count in counts.values())}', flush=True)
print(f'ticked={ticks > 0}', flush=True)

if ending == 'stop':
    initiator.stop()
    acceptor.stop()
    print(f'stopped={initiator.isStopped() and acceptor.isStopped()}', flush=True)
