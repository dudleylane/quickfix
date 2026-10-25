# The Java binding (#148): libquickfixjava, the JNI library compiled from the wrapper swig.sh generates, and
# quickfix.jar: the classes swig.sh generates under swig/, those spec/GeneratorJava.rb generates under spec/
# (a class per field and per message), and the hand-written ones under main/. Both are built
# into ${QUICKFIX_LIB_OUTPUT_DIR}/java, where test-java.sh finds them.
include(UseJava)

add_library(quickfix_java SHARED QuickfixJava.cpp)
# src/swig holds MySQLStubs.h / PostgreSQLStubs.h, which the wrapper includes when those backends are off, as
# for the Python and Ruby modules.
target_include_directories(quickfix_java PRIVATE ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src/C++
                           ${PROJECT_SOURCE_DIR}/src/swig ${JNI_INCLUDE_DIRS})
target_link_libraries(quickfix_java PRIVATE ${PROJECT_NAME})
if (HAVE_SSL)
  target_compile_definitions(quickfix_java PRIVATE HAVE_SSL=1)
  target_link_libraries(quickfix_java PRIVATE ${OPENSSL_LIBRARIES})
endif ()
# The generated wrapper trips warnings the other bindings' builds turn off too.
target_compile_options(quickfix_java PRIVATE -Wno-deprecated-declarations -Wno-deprecated
                       -Wno-unused-but-set-variable -Wno-unused-parameter)
# Versioned like the Python module, so the file records the ABI it was built against; the JVM loads it
# through the libquickfixjava.so namelink (System.loadLibrary("quickfixjava")).
set_target_properties(quickfix_java PROPERTIES OUTPUT_NAME quickfixjava VERSION ${PROJECT_VERSION}
                      SOVERSION ${PROJECT_VERSION_MAJOR} LIBRARY_OUTPUT_DIRECTORY ${QUICKFIX_LIB_OUTPUT_DIR}/java)
foreach(cfg ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
  string(TOUPPER "${cfg}" cfg_up)
  set_target_properties(quickfix_java PROPERTIES "LIBRARY_OUTPUT_DIRECTORY_${cfg_up}" ${QUICKFIX_LIB_OUTPUT_DIR}/java)
endforeach()

# Every class under swig/, spec/ and main/: swig.sh and spec/generate.sh add and remove generated ones, so
# the list is globbed, and re-globbed on each build.
file(GLOB_RECURSE quickfix_java_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/swig/*.java
     ${CMAKE_CURRENT_SOURCE_DIR}/spec/*.java ${CMAKE_CURRENT_SOURCE_DIR}/main/*.java)
# --release 21: the owner's choice (2026-10-25). No lint here: most of these classes are SWIG's, and its
# finalize() (deprecated for removal) and this-escape are not ours to fix; a new javac lint category must not
# fail a build nobody changed. test-java.sh compiles the hand-written tests with -Xlint:all -Werror.
set(CMAKE_JAVA_COMPILE_FLAGS --release 21)
add_jar(quickfix_jar SOURCES ${quickfix_java_sources} OUTPUT_NAME quickfix OUTPUT_DIR ${QUICKFIX_LIB_OUTPUT_DIR}/java)

install(TARGETS quickfix_java LIBRARY DESTINATION lib)
install_jar(quickfix_jar DESTINATION share/java)
