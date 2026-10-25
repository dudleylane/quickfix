package com.dudleylane.quickfix.test;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.TimeUnit;

/**
 * What ends the process and what does not, each in a child JVM (Child). An exception a callback lets out,
 * other than one the engine expects of it, ends the process with status 1 and the exception printed; a
 * JVM whose main returns with transports running exits once the shutdown hook has stopped them.
 */
public class CallbackErrorTests {
    record Result(int status, String output) {}

    static Result child(String mode) throws IOException, InterruptedException {
        List<String> command = new ArrayList<>();
        command.add(ProcessHandle.current().info().command().orElse("java"));
        command.add("-cp");
        command.add(System.getProperty("java.class.path"));
        for (String property : new String[] {"com.dudleylane.quickfix.library", "quickfix.test.root"}) {
            command.add("-D" + property + "=" + System.getProperty(property));
        }
        command.add(Child.class.getName());
        command.add(mode);
        Process process = new ProcessBuilder(command).redirectErrorStream(true).start();
        if (!process.waitFor(60, TimeUnit.SECONDS)) {
            process.destroyForcibly();
            throw new AssertionError(mode + ": the child did not exit within 60 s");
        }
        return new Result(process.exitValue(), new String(process.getInputStream().readAllBytes(), StandardCharsets.UTF_8));
    }

    static void exitsWithTheException(String mode, String callback) throws Exception {
        Result result = child(mode);
        Check.equal(1, result.status(), mode + ": status; output:\n" + result.output());
        Check.isTrue(result.output().contains("an exception escaped " + callback), mode + ": names the callback:\n" + result.output());
        Check.isTrue(result.output().contains("IllegalStateException: thrown from " + callback.substring(callback.lastIndexOf(':') + 1)),
                     mode + ": prints the exception:\n" + result.output());
        Check.isTrue(!result.output().contains("Child: survived"), mode + ": nothing ran after it");
    }

    public void testAnExceptionFromFromAppEndsTheProcess() throws Exception {
        exitsWithTheException("fromApp", "FIX::Application::fromApp");
        exitsWithTheException("fromApp-threaded", "FIX::Application::fromApp");
    }

    public void testAnExceptionFromOnLogonEndsTheProcess() throws Exception {
        exitsWithTheException("onLogon", "FIX::Application::onLogon");
    }

    public void testAnExceptionFromALogEndsTheProcess() throws Exception {
        exitsWithTheException("onEvent", "FIX::Log::onEvent");
    }

    public void testMainReturningWithTransportsRunningExits() throws Exception {
        for (String mode : new String[] {"exit", "exit-threaded"}) {
            Result result = child(mode);
            Check.equal(0, result.status(), mode + ": status; output:\n" + result.output());
            Check.isTrue(result.output().contains("Child: returning from main"), mode + ": got there:\n" + result.output());
        }
    }
}
