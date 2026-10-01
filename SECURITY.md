# Security policy

## Reporting a vulnerability

Report security problems privately, through GitHub's private vulnerability reporting: use
**Report a vulnerability** on the repository's
[Security tab](https://github.com/dudleylane/quickfix/security), or go straight to
[the report form](https://github.com/dudleylane/quickfix/security/advisories/new).
Don't open a public issue, pull request or discussion for one: that discloses the problem before a
fix exists.

Report privately anything a remote peer can trigger, logged on or not: a crash, a stall of other
sessions or of the acceptor, unbounded memory or resource use, a memory-safety or thread-safety
defect, or a way around a session-level control such as `AllowedRemoteAddresses`. If you're not
sure whether something qualifies, report it privately anyway.

A useful report names the transport (`SocketAcceptor`, `ThreadedSocketAcceptor`, an SSL variant or
an initiator), the FIX version, the release or commit, and the smallest input or sequence of events
that reproduces the problem.

## What happens next

The report becomes a draft security advisory that only you and the maintainers can see. The fix is
worked out there, and the advisory is published together with the fix.

## Supported versions

Fixes go to `master` and the next release; there are no maintenance branches. Untagged builds of
`master` should be replaced by a release. Published advisories are listed under
[Security advisories](https://github.com/dudleylane/quickfix/security/advisories).
