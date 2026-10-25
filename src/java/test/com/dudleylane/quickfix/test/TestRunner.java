package com.dudleylane.quickfix.test;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.List;

/**
 * Runs every public no-argument test* method of the classes named on the command line, or of all the
 * suites when none is, and exits non-zero if any fails. A dependency-free stand-in for JUnit: the build
 * has javac and nothing else.
 */
public final class TestRunner {
    static final Class<?>[] SUITES = {FieldTests.class, MessageTests.class, SettingsTests.class,
                                      DataDictionaryTests.class, StoreTests.class, TransportTests.class,
                                      CallbackErrorTests.class, LifetimeTests.class, ExceptionListTests.class,
                                      GeneratedTests.class};

    public static void main(String[] args) throws Exception {
        List<Class<?>> suites = new ArrayList<>();
        if (args.length == 0) {
            suites.addAll(Arrays.asList(SUITES));
        } else {
            for (String name : args) {
                suites.add(Class.forName("com.dudleylane.quickfix.test." + name));
            }
        }
        int passed = 0;
        List<String> failed = new ArrayList<>();
        for (Class<?> suite : suites) {
            Method[] methods = suite.getMethods();
            Arrays.sort(methods, Comparator.comparing(Method::getName));
            for (Method method : methods) {
                if (!method.getName().startsWith("test") || method.getParameterCount() != 0
                    || Modifier.isStatic(method.getModifiers())) {
                    continue;
                }
                String name = suite.getSimpleName() + "." + method.getName();
                long start = System.nanoTime();
                try {
                    method.invoke(suite.getConstructor().newInstance());
                    ++passed;
                    System.out.printf("ok      %s (%d ms)%n", name, (System.nanoTime() - start) / 1_000_000);
                } catch (InvocationTargetException e) {
                    failed.add(name);
                    System.out.printf("FAILED  %s%n", name);
                    e.getCause().printStackTrace(System.out);
                }
            }
        }
        System.out.printf("%d passed, %d failed%n", passed, failed.size());
        for (String name : failed) {
            System.out.println("  failed: " + name);
        }
        System.exit(failed.isEmpty() ? 0 : 1);
    }
}
