# The Ruby binding, built against the quickfix target -- its include directories, the build tree's
# QuickFIXBuildConfig.h, and a rebuild whenever the library changes. make_ruby.sh builds the same
# module with mkmf and plain -I flags, for builds without CMake; that one is not rebuilt when the
# library changes and falls back to the default allocator whatever the build tree says (#118).
add_library(quickfix_ruby MODULE QuickfixRuby.cpp)

# src/swig holds MySQLStubs.h / PostgreSQLStubs.h, which the wrapper includes when those backends
# are off, as for the Python module.
target_include_directories(quickfix_ruby PRIVATE ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src/C++
                           ${PROJECT_SOURCE_DIR}/src/swig ${Ruby_INCLUDE_DIRS})
target_link_libraries(quickfix_ruby PRIVATE ${PROJECT_NAME} ${Ruby_LIBRARIES})
if (HAVE_SSL)
  target_compile_definitions(quickfix_ruby PRIVATE HAVE_SSL=1)
  target_link_libraries(quickfix_ruby PRIVATE ${OPENSSL_LIBRARIES})
endif ()
# The generated wrapper trips warnings mkmf's build turns off too.
target_compile_options(quickfix_ruby PRIVATE -Wno-deprecated-declarations -Wno-deprecated -Wno-register
                       -Wno-unused-but-set-variable)

# Ruby requires 'quickfix' as quickfix.so. Built into lib/ruby/, beside nothing else of the same
# name: test.sh puts that directory ahead of src/ruby, where make_ruby.sh's module would be.
set_target_properties(quickfix_ruby PROPERTIES PREFIX "" OUTPUT_NAME quickfix SUFFIX ".so"
                      LIBRARY_OUTPUT_DIRECTORY ${QUICKFIX_LIB_OUTPUT_DIR}/ruby)
foreach(cfg ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
  string(TOUPPER "${cfg}" cfg_up)
  set_target_properties(quickfix_ruby PROPERTIES "LIBRARY_OUTPUT_DIRECTORY_${cfg_up}" ${QUICKFIX_LIB_OUTPUT_DIR}/ruby)
endforeach()

# Installed into Ruby's site directories, as install.sh does. RbConfig gives them absolute -- on Fedora
# /usr/local/lib64/ruby/site_ruby and /usr/local/share/ruby/site_ruby, while Ruby's own prefix is /usr
# -- so each is made relative to CMAKE_INSTALL_PREFIX when it lies under it, and --prefix and DESTDIR
# then apply as for the rest of the install. A site directory outside the prefix stays absolute.
foreach(kind arch lib)
  execute_process(COMMAND ${Ruby_EXECUTABLE} -rrbconfig -e "print RbConfig::CONFIG['site${kind}dir']"
                  OUTPUT_VARIABLE quickfix_ruby_dir OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE quickfix_ruby_result)
  if (NOT quickfix_ruby_result EQUAL 0 OR quickfix_ruby_dir STREQUAL "")
    message(FATAL_ERROR "could not ask ${Ruby_EXECUTABLE} for its site${kind}dir")
  endif ()
  cmake_path(IS_PREFIX CMAKE_INSTALL_PREFIX "${quickfix_ruby_dir}" NORMALIZE quickfix_ruby_under_prefix)
  if (quickfix_ruby_under_prefix)
    cmake_path(RELATIVE_PATH quickfix_ruby_dir BASE_DIRECTORY "${CMAKE_INSTALL_PREFIX}")
  endif ()
  set(quickfix_ruby_${kind}_default "${quickfix_ruby_dir}")
endforeach()
set(QUICKFIX_RUBY_ARCH_INSTALL_DIR "${quickfix_ruby_arch_default}" CACHE STRING
    "Where quickfix.so is installed: Ruby's sitearchdir, relative to CMAKE_INSTALL_PREFIX when under it")
set(QUICKFIX_RUBY_LIB_INSTALL_DIR "${quickfix_ruby_lib_default}" CACHE STRING
    "Where the quickfix*.rb files are installed: Ruby's sitelibdir, relative to CMAKE_INSTALL_PREFIX when under it")

install(TARGETS quickfix_ruby DESTINATION ${QUICKFIX_RUBY_ARCH_INSTALL_DIR})
install(DIRECTORY ${PROJECT_SOURCE_DIR}/src/ruby/ DESTINATION ${QUICKFIX_RUBY_LIB_INSTALL_DIR}
        FILES_MATCHING PATTERN "quickfix*.rb"
        PATTERN test EXCLUDE)
