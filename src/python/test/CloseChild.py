# Run by CloseTestCase in a process of its own. close() ends a transport deterministically (#207): it stops it and
# deletes it, so its sessions are unregistered (a later transport for the same SessionID then constructs), its
# factories are let go, and a method on it afterward raises ReferenceError rather than reaching freed memory. Each
# mode prints key=value lines; each child is its own process, so a transport left running cannot take the suite down.
import socket
import sys
import threading
import time

import quickfix as fix


class App(fix.Application):
    def __init__(self):
        super().__init__()
        self.on_logon = None
        self.on_logout = None
        self.on_toapp = None

    def onCreate(self, sessionID): pass
    def onLogon(self, sessionID):
        if self.on_logon:
            self.on_logon()
    def onLogout(self, sessionID):
        if self.on_logout:
            self.on_logout()
    def toAdmin(self, message, sessionID): pass
    def fromAdmin(self, message, sessionID): pass
    def toApp(self, message, sessionID):
        if self.on_toapp:
            self.on_toapp()
    def fromApp(self, message, sessionID): pass


class QuietLog(fix.Log):
    def clear(self): pass
    def backup(self): pass
    def onIncoming(self, text): pass
    def onOutgoing(self, text): pass
    def onEvent(self, text): pass


# Refers to its transport, so the #201 pin keeps it (and its sessions registered) until close.
class KeepingLogFactory(fix.LogFactory):
    def __init__(self):
        super().__init__()
        self.transport = None

    def create(self, *args):
        return QuietLog()

    def destroy(self, log): pass


def app_message():
    message = fix.Message()
    message.getHeader().setField(fix.StringField(35, 'D'))  # MsgType=D, an app message -> toApp
    return message


def free_port():
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        return probe.getsockname()[1]


def settings(kind, port, sender, target):
    result = fix.SessionSettings()
    connection = (f'ConnectionType=acceptor\nSocketAcceptPort={port}\n' if kind == 'acceptor' else
                  f'ConnectionType=initiator\nSocketConnectHost=127.0.0.1\nSocketConnectPort={port}\n'
                  'ReconnectInterval=1\nHeartBtInt=30\n')
    result.setFromString('[DEFAULT]\nNonStopSession=Y\nUseDataDictionary=N\n' + connection +
                         f'[SESSION]\nBeginString=FIX.4.2\nSenderCompID={sender}\nTargetCompID={target}\n')
    return result


def quiet():
    return fix.ScreenLogFactory(False, False, False)


def store():
    return fix.MemoryStoreFactory()


def name_of(call):
    # Returns the result of call(), or the name of the exception it raised.
    try:
        return call()
    except Exception as exc:
        return type(exc).__name__


def say(key, value):
    print(f'{key}={value}', flush=True)


mode = sys.argv[1]
print(f'module={fix.__file__}', flush=True)

if mode in ('reactor', 'threaded'):
    # A factory that refers to its transport keeps it (and ACC->INI registered) until close. After close a second
    # acceptor for the same SessionID constructs; a second close is a no-op; a method raises ReferenceError.
    acceptor_cls = fix.SocketAcceptor if mode == 'reactor' else fix.ThreadedSocketAcceptor
    port = free_port()
    f = KeepingLogFactory()
    t = acceptor_cls(App(), store(), settings('acceptor', port, 'ACC', 'INI'), f)
    f.transport = t
    t.start()
    say('second_before_close', name_of(
        lambda: (acceptor_cls(App(), store(), settings('acceptor', free_port(), 'ACC', 'INI'), quiet()), 'constructed')[1]))
    t.close()
    say('second_after_close', name_of(
        lambda: (acceptor_cls(App(), store(), settings('acceptor', free_port(), 'ACC', 'INI'), quiet()), 'constructed')[1]))
    say('second_close', repr(t.close()))
    say('method_after_close', name_of(lambda: (t.isStopped(), 'ok')[1]))

elif mode in ('callback_reactor', 'callback_threaded'):
    # onLogon calls close on the transport calling it: that would delete the transport under its own stack, so close
    # raises RuntimeError (the in-callback refusal), on any thread.
    acceptor_cls = fix.SocketAcceptor if mode == 'callback_reactor' else fix.ThreadedSocketAcceptor
    initiator_cls = fix.SocketInitiator if mode == 'callback_reactor' else fix.ThreadedSocketInitiator
    port = free_port()
    result = []
    app = App()
    acceptor = acceptor_cls(app, store(), settings('acceptor', port, 'ACC', 'INI'), quiet())
    app.on_logon = lambda: result.append(name_of(lambda: (acceptor.close(), 'closed(no-refusal)')[1]))
    initiator = initiator_cls(App(), store(), settings('initiator', port, 'INI', 'ACC'), quiet())
    acceptor.start()
    initiator.start()
    deadline = time.time() + 30
    while not result and time.time() < deadline:
        time.sleep(0.05)
    say('callback', result[0] if result else 'timed out')
    # The callback could not close its own transport; the main thread can, and must, or a transport still running
    # at the interpreter's exit crashes (exitOnCallbackError's comment).
    initiator.close()
    acceptor.close()

elif mode == 'logged_on':
    # A logged-on reactor acceptor, closed from another thread: its session is unregistered, so a second acceptor
    # for it constructs.
    port = free_port()
    acceptor = fix.SocketAcceptor(App(), store(), settings('acceptor', port, 'ACC', 'INI'), quiet())
    initiator = fix.SocketInitiator(App(), store(), settings('initiator', port, 'INI', 'ACC'), quiet())
    acceptor.start()
    initiator.start()
    deadline = time.time() + 20
    while not acceptor.isLoggedOn() and time.time() < deadline:
        time.sleep(0.05)
    say('logged_on', acceptor.isLoggedOn())
    initiator.close()
    acceptor.close()  # from the main thread, while block() runs on the reactor thread
    say('second_after_close', name_of(
        lambda: (fix.SocketAcceptor(App(), store(), settings('acceptor', free_port(), 'ACC', 'INI'), quiet()), 'constructed')[1]))

elif mode == 'sendtotarget':
    # A toApp reached through Session.sendToTarget runs on the caller's own thread, holding the GIL. close() from it
    # would delete the transport under sendToTarget's stack -- a use-after-free -- so the in-callback marker makes it
    # raise; sendToTarget then returns, and the transport is still closable from the main thread.
    port = free_port()
    app = App()
    ini = fix.SocketInitiator(app, store(), settings('initiator', port, 'INI', 'ACC'), quiet())
    callback = []
    app.on_toapp = lambda: callback.append(name_of(lambda: (ini.close(), 'closed(no-refusal)')[1]))
    sid = fix.SessionID('FIX.4.2', 'INI', 'ACC')
    say('sendtotarget', name_of(lambda: (fix.Session.sendToTarget(app_message(), sid), 'returned')[1]))
    say('callback_close', callback[0] if callback else 'no-callback')
    say('close_from_main', name_of(lambda: (ini.close(), 'closed')[1]))

elif mode == 'donotsend':
    # A toApp that raises DoNotSend (which the engine expects) unwinds as a C++ exception through the in-callback
    # guard. The guard's destructor must decrement, or close() from the main thread afterward would see a callback
    # still in progress and raise. close_from_main succeeding proves it did.
    port = free_port()
    app = App()
    ini = fix.SocketInitiator(app, store(), settings('initiator', port, 'INI', 'ACC'), quiet())
    def raise_donotsend():
        raise fix.DoNotSend()
    app.on_toapp = raise_donotsend
    sid = fix.SessionID('FIX.4.2', 'INI', 'ACC')
    say('donotsend', name_of(lambda: (fix.Session.sendToTarget(app_message(), sid), 'returned')[1]))
    say('close_from_main', name_of(lambda: (ini.close(), 'closed')[1]))

elif mode == 'logout_close':
    # A threaded acceptor logged on, closed from the main thread. close()'s stop() logs the session out, so onLogout
    # runs on the connection thread while main holds the close lock. Its close() must raise at once -- the
    # in-callback refusal is before the lock -- rather than block on the lock the main thread holds while its stop()
    # joins the connection thread this callback runs for (#111). Without that, main's close() would deadlock.
    port = free_port()
    acc_app = App()
    acceptor = fix.ThreadedSocketAcceptor(acc_app, store(), settings('acceptor', port, 'ACC', 'INI'), quiet())
    initiator = fix.ThreadedSocketInitiator(App(), store(), settings('initiator', port, 'INI', 'ACC'), quiet())
    hook = []
    acc_app.on_logout = lambda: hook.append(name_of(lambda: (acceptor.close(), 'closed(no-refusal)')[1]))
    acceptor.start()
    initiator.start()
    deadline = time.time() + 20
    while not acceptor.isLoggedOn() and time.time() < deadline:
        time.sleep(0.05)
    say('logged_on', acceptor.isLoggedOn())
    main_close = []
    worker = threading.Thread(target=lambda: main_close.append(name_of(lambda: (acceptor.close(), 'closed')[1])), daemon=True)
    worker.start()
    worker.join(20)
    say('logout_close', hook[0] if hook else 'no-callback')
    say('main_close', main_close[0] if main_close else 'timed out')
    initiator.close()

elif mode == 'initiator':
    # close an initiator (the other transport kind): its session unregisters, so a second initiator for it
    # constructs, a method afterward raises, and the pin is released.
    port = free_port()
    f = KeepingLogFactory()
    ini = fix.SocketInitiator(App(), store(), settings('initiator', port, 'INI', 'ACC'), f)
    f.transport = ini
    ini.start()
    ini.close()
    say('method_after_close', name_of(lambda: (ini.isStopped(), 'ok')[1]))

elif mode == 'running':
    # A Python program can run block() on a thread of its own (start() uses the engine's own threads, which stop()
    # joins). While a thread is inside block(), the destroy helper refuses to delete the transport -- that would
    # free it under block()'s stack -- and reports it still running, leaving it owned for the garbage collector.
    # close() then stops it (block() returns) and deletes it.
    port = free_port()
    acceptor = fix.SocketAcceptor(App(), store(), settings('acceptor', port, 'ACC', 'INI'), quiet())
    blocker = threading.Thread(target=acceptor.block, daemon=True)
    blocker.start()
    deadline = time.time() + 20
    while not fix._quickfixBlocking(acceptor) and time.time() < deadline:
        time.sleep(0.01)
    say('blocking', fix._quickfixBlocking(acceptor))
    say('destroy_while_blocking', fix._quickfixDestroyAcceptor(acceptor))  # 1: refused, not deleted
    say('cleaned', name_of(lambda: (acceptor.close(), 'closed')[1]))
    say('method_after_close', name_of(lambda: (acceptor.isStopped(), 'ok')[1]))
    blocker.join(5)

elif mode == 'failed_start':
    # start() on a port already bound fails synchronously (the engine binds in start()). The transport never ran,
    # so close() deletes it cleanly and a method afterward raises.
    port = free_port()
    held = socket.socket()
    held.bind(('127.0.0.1', port))
    held.listen(1)
    acceptor = fix.SocketAcceptor(App(), store(), settings('acceptor', port, 'ACC', 'INI'), quiet())
    say('failed_start', name_of(lambda: (acceptor.start(), 'started')[1]))
    say('close', name_of(lambda: (acceptor.close(), 'closed')[1]))
    say('method_after_close', name_of(lambda: (acceptor.isStopped(), 'ok')[1]))
    held.close()

elif mode == 'expiry':
    # A close() whose timeout expires with the helper still reporting the transport running raises RuntimeError and
    # does NOT mark it closed, so a later real close() still deletes it. Force the helper to always report running.
    port = free_port()
    acc = fix.SocketAcceptor(App(), store(), settings('acceptor', port, 'ACC', 'INI'), quiet())
    acc.start()
    real_destroy = fix._quickfixDestroyAcceptor
    fix._quickfixDestroyAcceptor = lambda transport: 1  # always "another thread is inside block()"
    say('expiry', name_of(lambda: (acc.close(0.1), 'closed')[1]))  # RuntimeError, not marked closed
    fix._quickfixDestroyAcceptor = real_destroy
    say('cleaned', name_of(lambda: (acc.close(), 'closed')[1]))    # the real helper deletes it
    say('method_after_close', name_of(lambda: (acc.isStopped(), 'ok')[1]))

elif mode == 'aftergc':
    # After close() the garbage collector must not delete the transport again -- close cleared the proxy's
    # ownership, not only its pointer -- and a session view taken before close raises rather than reaching freed
    # memory (#197). Run under ASan, a double delete or a use of the freed session is a sanitizer report.
    import gc
    port = free_port()
    acc = fix.SocketAcceptor(App(), store(), settings('acceptor', port, 'ACC', 'INI'), quiet())
    session = acc.getSession(fix.SessionID('FIX.4.2', 'ACC', 'INI'))  # a #197 view of a session close will free
    acc.start()
    acc.close()
    say('view_after_close', name_of(lambda: (session.isLoggedOn(), 'ok')[1]))  # ReferenceError, not freed memory
    del acc, session
    gc.collect()
    say('after_gc', 'ok')  # no second delete of the transport the proxy no longer owns

say('pins', fix._quickfixPinCount())
print('end=true', flush=True)
