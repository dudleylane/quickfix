set(quickfix_python_SOURCES QuickfixPython.cpp)
set(quickfix_python_lib_name _${PROJECT_NAME})

add_library(${quickfix_python_lib_name} SHARED  ${quickfix_python_SOURCES})

# src/swig holds MySQLStubs.h / PostgreSQLStubs.h, which the generated wrapper
# includes on the #else branch whenever HAVE_MYSQL / HAVE_POSTGRESQL are off --
# the default. Without it the module does not compile, which is the second half
# of why -DHAVE_PYTHON3=ON never produced anything.
target_include_directories(${quickfix_python_lib_name} PRIVATE ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src/C++ ${PROJECT_SOURCE_DIR}/src/swig)

target_link_libraries(${quickfix_python_lib_name} ${PROJECT_NAME})

if (HAVE_SSL)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DHAVE_SSL=1")
  target_link_libraries(${quickfix_python_lib_name} ${PROJECT_NAME} ${OPENSSL_LIBRARIES})
endif()

# The module takes the library's version from project() in the top-level CMakeLists.txt, read as
# PROJECT_VERSION* like the library's (see src/C++/CMakeLists.txt), so its soname names the ABI it
# was built against.
set_target_properties(${quickfix_python_lib_name} PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR} PREFIX "")

# Installed where the interpreter looks for a given prefix: its own platlib, asked of sysconfig with
# the prefix left out, so the cache value stays relative and --prefix and DESTDIR still apply. Here
# that is lib64/python3.12/site-packages. This used to be lib/python3, which no interpreter searches,
# so `import quickfix` failed after an install (#108). FindPython's Python3_SITEARCH is no substitute:
# it is absolute and ignores CMAKE_INSTALL_PREFIX. STRING rather than PATH, which some frontends make
# absolute. The module keeps its versioned file name; Python imports it through the _quickfix.so
# namelink, as test-python3.sh does from lib/.
execute_process(
  COMMAND ${Python3_EXECUTABLE} -c
          "import sysconfig; print(sysconfig.get_path('platlib', vars={'base': '.', 'platbase': '.'}).removeprefix('./'))"
  OUTPUT_VARIABLE quickfix_python_platlib OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE quickfix_python_result)
if (NOT quickfix_python_result EQUAL 0 OR quickfix_python_platlib STREQUAL "" OR IS_ABSOLUTE "${quickfix_python_platlib}")
  message(FATAL_ERROR "could not ask ${Python3_EXECUTABLE} for its platlib relative to a prefix "
                      "(got '${quickfix_python_platlib}'); set QUICKFIX_PYTHON3_INSTALL_DIR")
endif()
set(QUICKFIX_PYTHON3_INSTALL_DIR "${quickfix_python_platlib}" CACHE STRING
    "Where the Python module is installed, relative to CMAKE_INSTALL_PREFIX")

install(TARGETS ${quickfix_python_lib_name} DESTINATION ${QUICKFIX_PYTHON3_INSTALL_DIR})
install(DIRECTORY ${PROJECT_SOURCE_DIR}/src/python/ DESTINATION ${QUICKFIX_PYTHON3_INSTALL_DIR}
        FILES_MATCHING PATTERN "quickfix*.py"
        PATTERN test EXCLUDE
        PATTERN __pycache__ EXCLUDE)
