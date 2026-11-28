import os
import subprocess
import sys
import unittest


# close() ends a transport deterministically (#207): a script whose factory or application refers to its transport --
# which the #201 pin keeps alive, and its sessions registered -- can close it rather than wait for the garbage
# collector. close stops it and deletes it; afterward a second transport for the same SessionID constructs, a second
# close is a no-op, and a method raises ReferenceError. It refuses from inside a callback, on any thread, which would
# delete it under its own stack. Each mode runs in a process of its own, so a transport left running cannot take the
# suite down with it.
class CloseTestCase(unittest.TestCase):

    def child(self, mode):
        script = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'CloseChild.py')
        result = subprocess.run([sys.executable, script, mode], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, timeout=120)
        self.assertEqual(0, result.returncode, result.stdout)
        self.assertNotIn('Sanitizer', result.stdout)
        lines = dict(line.split('=', 1) for line in result.stdout.splitlines() if '=' in line)
        self.assertEqual('true', lines.get('end'), result.stdout)
        return lines, result.stdout

    def discriminates(self, mode):
        lines, output = self.child(mode)
        # Kept by its factory, its session stays registered, so a second acceptor for it fails to construct...
        self.assertEqual('ConfigError', lines.get('second_before_close'), output)
        # ...until close() unregisters it, after which one constructs.
        self.assertEqual('constructed', lines.get('second_after_close'), output)
        self.assertEqual('None', lines.get('second_close'), output)  # a second close is a no-op
        self.assertEqual('ReferenceError', lines.get('method_after_close'), output)
        self.assertEqual('0', lines.get('pins'), output)  # the #201 pin returned to baseline

    def test_close_unregisters_a_reactor_acceptors_session(self):
        self.discriminates('reactor')

    def test_close_unregisters_a_threaded_acceptors_session(self):
        self.discriminates('threaded')

    def test_close_from_a_reactor_callback_raises(self):
        lines, output = self.child('callback_reactor')
        self.assertEqual('RuntimeError', lines.get('callback'), output)
        self.assertEqual('0', lines.get('pins'), output)

    def test_close_from_a_threaded_callback_raises(self):
        lines, output = self.child('callback_threaded')
        self.assertEqual('RuntimeError', lines.get('callback'), output)
        self.assertEqual('0', lines.get('pins'), output)

    def test_close_unregisters_a_logged_on_acceptor(self):
        lines, output = self.child('logged_on')
        self.assertEqual('True', lines.get('logged_on'), output)
        self.assertEqual('constructed', lines.get('second_after_close'), output)
        self.assertEqual('0', lines.get('pins'), output)

    def test_close_from_a_sendtotarget_toApp_raises(self):
        lines, output = self.child('sendtotarget')
        self.assertEqual('RuntimeError', lines.get('callback_close'), output)  # the in-callback marker
        self.assertEqual('returned', lines.get('sendtotarget'), output)        # sendToTarget did not run on freed memory
        self.assertEqual('closed', lines.get('close_from_main'), output)       # still closable from outside the callback
        self.assertEqual('0', lines.get('pins'), output)

    def test_close_after_a_raising_toApp_succeeds(self):
        lines, output = self.child('donotsend')
        # The in-callback guard decremented as DoNotSend unwound through it, so close() from the main thread
        # afterward does not see a callback still in progress: it succeeds rather than raise.
        self.assertEqual('returned', lines.get('donotsend'), output)
        self.assertEqual('closed', lines.get('close_from_main'), output)
        self.assertEqual('0', lines.get('pins'), output)

    def test_close_from_onLogout_does_not_deadlock(self):
        lines, output = self.child('logout_close')
        self.assertEqual('True', lines.get('logged_on'), output)
        self.assertEqual('RuntimeError', lines.get('logout_close'), output)    # the callback's close refused at once
        self.assertEqual('closed', lines.get('main_close'), output)            # not "timed out": no #111 deadlock
        self.assertEqual('0', lines.get('pins'), output)

    def test_close_an_initiator(self):
        lines, output = self.child('initiator')
        self.assertEqual('ReferenceError', lines.get('method_after_close'), output)
        self.assertEqual('0', lines.get('pins'), output)

    def test_close_refuses_while_another_thread_is_in_block(self):
        lines, output = self.child('running')
        self.assertEqual('True', lines.get('blocking'), output)
        self.assertEqual('1', lines.get('destroy_while_blocking'), output)     # refused, not freed under block()
        self.assertEqual('closed', lines.get('cleaned'), output)
        self.assertEqual('ReferenceError', lines.get('method_after_close'), output)
        self.assertEqual('0', lines.get('pins'), output)

    def test_close_after_a_failed_start(self):
        lines, output = self.child('failed_start')
        self.assertEqual('RuntimeError', lines.get('failed_start'), output)    # the bind failure, raised by start
        self.assertEqual('closed', lines.get('close'), output)                 # deleted despite it
        self.assertEqual('ReferenceError', lines.get('method_after_close'), output)
        self.assertEqual('0', lines.get('pins'), output)

    def test_close_that_times_out_does_not_mark_the_transport_closed(self):
        lines, output = self.child('expiry')
        self.assertEqual('RuntimeError', lines.get('expiry'), output)          # timed out with block() still held
        self.assertEqual('closed', lines.get('cleaned'), output)               # a later real close() still deletes it
        self.assertEqual('ReferenceError', lines.get('method_after_close'), output)
        self.assertEqual('0', lines.get('pins'), output)

    def test_the_gc_does_not_delete_a_closed_transport_again(self):
        lines, output = self.child('aftergc')
        self.assertEqual('ReferenceError', lines.get('view_after_close'), output)  # a #197 view, ended by close
        self.assertEqual('ok', lines.get('after_gc'), output)                      # no second delete
        self.assertEqual('0', lines.get('pins'), output)


if __name__ == '__main__':
    unittest.main()
