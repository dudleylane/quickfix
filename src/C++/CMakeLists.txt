add_subdirectory(test)

set(quickfix_SOURCES
  Acceptor.cpp
  DataDictionary.cpp
  DataDictionaryProvider.cpp
  Dictionary.cpp
  FieldConvertors.cpp
  FieldMap.cpp
  FieldTypes.cpp
  FileLog.cpp
  FileStore.cpp
  Group.cpp
  HostDetailsProvider.cpp
  HttpConnection.cpp
  HttpMessage.cpp
  HttpParser.cpp
  HttpServer.cpp
  Initiator.cpp
  Log.cpp
  Message.cpp
  MessageSorters.cpp
  MessageStore.cpp
  NullStore.cpp
  Parser.cpp
  pugixml.cpp
  PUGIXML_DOMDocument.cpp
  Session.cpp
  SessionFactory.cpp
  SessionSettings.cpp
  Settings.cpp
  SocketAcceptor.cpp
  SocketConnection.cpp
  SocketConnector.cpp
  SocketInitiator.cpp
  SocketServer.cpp
  ThreadedSocketAcceptor.cpp
  ThreadedSocketConnection.cpp
  ThreadedSocketInitiator.cpp
  TimeRange.cpp
  Utility.cpp
)

list(APPEND quickfix_SOURCES SocketMonitor_UNIX.cpp)

if (HAVE_SSL)
  set (quickfix_SOURCES ${quickfix_SOURCES}
    SSLSocketAcceptor.cpp
    SSLSocketConnection.cpp
    SSLSocketInitiator.cpp
    ThreadedSSLSocketAcceptor.cpp
    ThreadedSSLSocketConnection.cpp
    ThreadedSSLSocketInitiator.cpp
    UtilitySSL.cpp)
endif()

if (HAVE_MYSQL)
  list(APPEND quickfix_SOURCES MySQLLog.cpp MySQLStore.cpp)
endif()

if (HAVE_POSTGRESQL)
  list(APPEND quickfix_SOURCES PostgreSQLLog.cpp PostgreSQLStore.cpp)
endif()

if (HAVE_ODBC)
  list(APPEND quickfix_SOURCES OdbcLog.cpp OdbcStore.cpp)
endif()

if (WIN32)
  add_library(${PROJECT_NAME} STATIC  ${quickfix_SOURCES})
  set_target_properties (${PROJECT_NAME} PROPERTIES DEBUG_POSTFIX d)
  target_link_libraries(${PROJECT_NAME} ${OPENSSL_LIBRARIES} ${MYSQL_CLIENT_LIBS} ${PostgreSQL_LIBRARIES} ws2_32 crypt32)
else()
   if (QUICKFIX_SHARED_LIBS)
	   add_library(${PROJECT_NAME} SHARED  ${quickfix_SOURCES})
   else()
	   add_library(${PROJECT_NAME} STATIC  ${quickfix_SOURCES})
  endif()
  target_link_libraries(${PROJECT_NAME} ${OPENSSL_LIBRARIES} ${MYSQL_CLIENT_LIBS} ${PostgreSQL_LIBRARIES} ${ODBC_LIBRARIES} pthread)
endif()

if(ENABLE_TBB_ALLOCATOR)
  find_package(TBB REQUIRED COMPONENTS tbbmalloc)
  target_compile_definitions(${PROJECT_NAME} PUBLIC ENABLE_TBB_ALLOCATOR)
  target_link_libraries(${PROJECT_NAME} TBB::tbbmalloc)
  message("-- Building with TBB scalable allocator")
endif()

# QuickFIXBuildConfig.h records, for consumers, the build options that change code they compile
# inline -- today ENABLE_TBB_ALLOCATOR, which selects the allocator behind FieldMap::Fields and the
# socket send queues. A compile definition reaches only targets in this build; without the header,
# installed headers are identical to a default build's. It is generated per build tree, since two
# trees sharing this source tree can differ, and installed beside Utility.h, which includes it.
set(QUICKFIX_TBB_ALLOCATOR ${ENABLE_TBB_ALLOCATOR})
configure_file(QuickFIXBuildConfig.h.in ${PROJECT_BINARY_DIR}/generated/QuickFIXBuildConfig.h @ONLY)
target_include_directories(${PROJECT_NAME} PUBLIC $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>)
install(FILES ${PROJECT_BINARY_DIR}/generated/QuickFIXBuildConfig.h DESTINATION include/quickfix)

include(CheckFunctionExists)
check_symbol_exists(ftime "sys/timeb.h" HAVE_FTIME)
if(HAVE_FTIME)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_FTIME)
endif()

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)
target_include_directories(${PROJECT_NAME} INTERFACE ${PROJECT_SOURCE_DIR}/include)
target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/src/C++)

# The version, and its major as the soname, come from project() in the top-level CMakeLists.txt,
# which defines quickfix_VERSION and quickfix_VERSION_MAJOR. Don't set them here: a second copy
# shadows project() and keeps the old soname without a warning. The soname is 18 rather than
# upstream's 17 because the fork changed the layout of exported classes (FieldBase, Message), so
# a binary built against upstream 17 must not load this library. Since v18.0.0, a layout change
# to an exported class bumps the major in project().
set_target_properties(${PROJECT_NAME} PROPERTIES VERSION ${quickfix_VERSION} SOVERSION ${quickfix_VERSION_MAJOR} )

install(TARGETS ${PROJECT_NAME} DESTINATION lib)

get_target_property(debug_postfix ${PROJECT_NAME} DEBUG_POSTFIX)
if(${debug_postfix} STREQUAL debug_postfix-NOTFOUND)
   set (debug_postfix "")
else()
   if(CMAKE_BUILD_TYPE)
       string(TOUPPER ${CMAKE_BUILD_TYPE} build_type)
       if(NOT ${build_type} STREQUAL DEBUG)
          set (debug_postfix "")
       endif()
   endif()
endif()
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}${debug_postfix}.pdb:${PROJECT_NAME} DESTINATION lib OPTIONAL)


install(DIRECTORY ${PROJECT_SOURCE_DIR}/src/C++/ DESTINATION include/quickfix
        FILES_MATCHING PATTERN "*.h"
        PATTERN double-conversion EXCLUDE
        PATTERN test EXCLUDE)
