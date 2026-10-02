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

install(TARGETS ${quickfix_python_lib_name} DESTINATION lib/python3)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/src/python/ DESTINATION lib/python3
        FILES_MATCHING PATTERN "quickfix*.py"
        PATTERN test EXCLUDE)
