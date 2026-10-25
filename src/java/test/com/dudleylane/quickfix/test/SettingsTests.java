package com.dudleylane.quickfix.test;

import com.dudleylane.quickfix.*;

public class SettingsTests {
    public void testParse() {
        SessionSettings settings = Fixture.acceptorSettings(12345, "SERVER", "CLIENT", false);
        Check.equal(1L, settings.size(), "one session");
        Dictionary session = settings.get(new SessionID("FIX.4.2", "SERVER", "CLIENT"));
        Check.equal(12345, session.getInt("SocketAcceptPort"), "inherited from [DEFAULT]");
        Check.equal("acceptor", session.getString("ConnectionType"), "string");
        Check.isTrue(!session.getBool("UseDataDictionary"), "bool");
    }

    public void testAMissingKeyIsConfigError() {
        Dictionary dictionary = new Dictionary();
        Check.raises(ConfigError.class, () -> dictionary.getString("NoSuchKey"));
    }

    public void testAnUnknownSessionIsConfigError() {
        SessionSettings settings = Fixture.acceptorSettings(12345, "SERVER", "CLIENT", false);
        Check.raises(ConfigError.class, () -> settings.get(new SessionID("FIX.4.2", "NOBODY", "CLIENT")));
    }
}
