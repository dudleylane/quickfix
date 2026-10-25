package com.dudleylane.quickfix.test;

import java.nio.file.Files;
import java.util.Set;
import java.util.TreeSet;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * The engine's exceptions are listed four times: in Exceptions.h, as the hand-written classes under main/,
 * and twice in src/java/quickfix.i -- the classes JNI_OnLoad finds (g_exceptions) and those throwToJava
 * matches. An exception added to Exceptions.h and not to the others would reach Java as a bare
 * FIXException with nothing failing, so the lists are compared here.
 */
public class ExceptionListTests {
    static Set<String> matches(String text, String regex) {
        Set<String> names = new TreeSet<>();
        Matcher matcher = Pattern.compile(regex).matcher(text);
        while (matcher.find()) {
            names.add(matcher.group(1));
        }
        return names;
    }

    public void testEveryEngineExceptionHasItsJavaClass() throws Exception {
        String header = Files.readString(Fixture.root().resolve("src/C++/Exceptions.h"));
        Set<String> engine = matches(header, "struct (\\w+) : public \\w*Exception\\b");
        engine.add("FIXException"); // FIX::Exception itself
        Check.isTrue(engine.size() > 30, "found the engine's exceptions: " + engine);

        Set<String> java = new TreeSet<>();
        try (var files = Files.list(Fixture.root().resolve("src/java/main/com/dudleylane/quickfix"))) {
            for (var file : (Iterable<java.nio.file.Path>) files::iterator) {
                String source = Files.readString(file);
                Matcher m = Pattern.compile("class (\\w+) extends (FIXException|SocketException|RuntimeException)\\b").matcher(source);
                if (m.find()) {
                    java.add(m.group(1));
                }
            }
        }
        Check.equal(engine, java, "a Java class for each of Exceptions.h's, and no other");

        String binding = Files.readString(Fixture.root().resolve("src/java/quickfix.i"));
        int start = binding.indexOf("ExceptionClass g_exceptions[] = {");
        String loaded = binding.substring(start, binding.indexOf("};", start));
        Check.equal(engine, matches(loaded, "\\{\"(\\w+)\"\\}"), "JNI_OnLoad finds each");

        Set<String> thrown = matches(binding, "QUICKFIX_JAVA_MATCH(?:_FIELD)?\\((\\w+)\\)\\s*\\n");
        thrown.add("DataDictionaryNotFound"); // matched by hand, for its version
        thrown.add("FIXException");           // the fallback
        Check.equal(engine, thrown, "throwToJava matches each");

        Set<String> ignored = matches(binding, "%ignore FIX::(\\w+);");
        Set<String> expectedIgnored = new TreeSet<>(engine);
        expectedIgnored.remove("FIXException");
        expectedIgnored.add("Exception");
        Check.isTrue(ignored.containsAll(expectedIgnored), "quickfix.i keeps SWIG from wrapping each");
    }
}
