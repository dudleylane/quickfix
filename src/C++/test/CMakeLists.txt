add_definitions(-DCATCH_AMALGAMATED_CUSTOM_MAIN)

if (QUICKFIX_TESTS)
  set (ut_SOURCES 
  ${PROJECT_SOURCE_DIR}/src/C++/test/catch_amalgamated.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/DataDictionaryProviderTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/DataDictionaryTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/DictionaryTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/FieldBaseTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/FieldConvertorsTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/FieldMapTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/FieldTypesTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/FileLogTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/FileStoreFactoryTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/FileStoreTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/FileUtilitiesTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/GroupTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/HostDetailsProviderTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/HttpMessageTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/HttpParserTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/MemoryStoreTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/MessageSortersTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/MessagesTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/MySQLStoreTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/NullStoreTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/OdbcStoreTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/ParserTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/PostgreSQLStoreTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/SessionFactoryTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/SessionIDTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/SessionSettingsTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/SessionStateTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/SessionTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/SettingsTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/SharedArrayTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/SocketAcceptorTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/SocketConnectionTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/SocketConnectorTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/SocketMonitorTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/SocketServerTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/StringUtilitiesTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/TestHelper.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/ThreadedTransportTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/TimeRangeTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/UtcTimeOnlyTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/UtcTimeStampTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/UtilityTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/UtilitySSLTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/VersionTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/SSLSocketAcceptorTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/SSLTransportTestCase.cpp)

  add_executable(ut ut.cpp ${ut_SOURCES})
  
  # src/C++ precedes the project root so a test's #include "config.h" finds
  # src/C++/config.h. Configure used to drop an empty placeholder at the root,
  # which otherwise won and compiled every SSL test out of existence; it no longer
  # does (#62), but a leftover one in an old checkout would still win.
  target_include_directories(ut PRIVATE ${PROJECT_SOURCE_DIR}/src/C++
    ${PROJECT_SOURCE_DIR})
    
  target_link_libraries(ut ${PROJECT_NAME})

  # Expected values for the generated headers, from the same CMake settings that generate them:
  # VersionTestCase checks quickfix/QuickFIXVersion.h against project(), and UtilityTests checks
  # QuickFIXBuildConfig.h against ENABLE_TBB_ALLOCATOR.
  target_compile_definitions(ut PRIVATE
    QUICKFIX_EXPECTED_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    QUICKFIX_EXPECTED_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    QUICKFIX_EXPECTED_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    QUICKFIX_EXPECTED_TBB_ALLOCATOR=$<BOOL:${ENABLE_TBB_ALLOCATOR}>)
  
  if (NOT WIN32)
  ADD_CUSTOM_TARGET(ut_target ALL
                    COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE:ut> ${PROJECT_SOURCE_DIR}/test/ut)
  else()
  set_target_properties(ut PROPERTIES
                        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${PROJECT_SOURCE_DIR}/test/debug/ut/
                        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${PROJECT_SOURCE_DIR}/test/release/ut/
                        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${PROJECT_SOURCE_DIR}/test/release/ut/)
  endif()  
endif()  