package com.dudleylane.quickfix.test;

/**
 * A SynchronizedApplication keeps the application it wraps (#208), so an application dropped by the script is still
 * reached through the wrapper rather than used from freed memory. Runs in a JVM of its own, so that a crash is a
 * failure here.
 */
public class SynchronizedApplicationTests {
    public void testWrappedApplicationSurvivesCollection() throws Exception {
        CallbackErrorTests.Result result = CallbackErrorTests.child(SynchronizedApplicationChild.class, "survive");
        Check.equal(0, result.status(), "status; output:\n" + result.output());
        Check.isTrue(result.output().contains("end=true"), "got to the end; output:\n" + result.output());
    }
}
