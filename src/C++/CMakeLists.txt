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

# The name an installed consumer gets from find_package(quickfix), for projects that add this tree
# with add_subdirectory or FetchContent instead.
add_library(quickfix::quickfix ALIAS ${PROJECT_NAME})

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
target_include_directories(${PROJECT_NAME} INTERFACE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/src/C++)

# The version, and its major as the soname, come from project() in the top-level CMakeLists.txt,
# read as PROJECT_VERSION*: the variables QuickFIXVersion.h, quickfix.pc, the package config and
# VersionTestCase read too (#64). project() also defines quickfix_VERSION*, but a set() of those in
# a subdirectory shadows them without a warning, so nothing reads them. The soname is the major
# version, 18 from the first release, because the fork changed the layout of exported classes
# (FieldBase, Message) from upstream's 17. Since v18.0.0, a layout change to an exported class
# bumps the major in project(); 19.0.0 was the first such bump, 20.0.0 the second. release_tag_check.yml compares the built
# library's soname with the release tag.
set_target_properties(${PROJECT_NAME} PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR} )

install(TARGETS ${PROJECT_NAME} EXPORT quickfixTargets DESTINATION lib)

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

# Discovery for installed consumers: a CMake package, find_package(quickfix 20) -> quickfix::quickfix,
# and a pkg-config file. Any 20.x satisfies a request for 20, since the major version is the soname.
# The package config finds the imported targets the exported link interface names (TBB, ODBC).
include(CMakePackageConfigHelpers)
install(EXPORT quickfixTargets NAMESPACE quickfix:: DESTINATION lib/cmake/quickfix)
configure_package_config_file(${PROJECT_SOURCE_DIR}/cmake/quickfixConfig.cmake.in
  ${PROJECT_BINARY_DIR}/quickfixConfig.cmake INSTALL_DESTINATION lib/cmake/quickfix)
write_basic_package_version_file(${PROJECT_BINARY_DIR}/quickfixConfigVersion.cmake
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES ${PROJECT_BINARY_DIR}/quickfixConfig.cmake ${PROJECT_BINARY_DIR}/quickfixConfigVersion.cmake
  DESTINATION lib/cmake/quickfix)

# The pkg-config file's Libs.private is the library's own link dependencies, which a static link of it
# needs, as -l flags. TBB goes in Libs instead: a consumer compiled against a TBB build calls
# tbb::scalable_allocator from inline code (QuickFIXBuildConfig.h), so it links tbbmalloc itself.
set(quickfix_pc_link_items ${OPENSSL_LIBRARIES} ${MYSQL_CLIENT_LIBS} ${PostgreSQL_LIBRARIES})
if(HAVE_ODBC)
  get_target_property(quickfix_odbc_location ODBC::ODBC IMPORTED_LOCATION)
  list(APPEND quickfix_pc_link_items ${quickfix_odbc_location})
endif()
set(QUICKFIX_PC_LIBS_PRIVATE "")
foreach(item IN LISTS quickfix_pc_link_items)
  get_filename_component(name "${item}" NAME_WE)
  string(REGEX REPLACE "^lib" "" name "${name}")
  string(APPEND QUICKFIX_PC_LIBS_PRIVATE " -l${name}")
endforeach()
string(APPEND QUICKFIX_PC_LIBS_PRIVATE " -lpthread")
set(QUICKFIX_PC_LIBS "")
if(ENABLE_TBB_ALLOCATOR)
  set(QUICKFIX_PC_LIBS " -ltbbmalloc")
endif()
configure_file(${PROJECT_SOURCE_DIR}/quickfix.pc.in ${PROJECT_BINARY_DIR}/quickfix.pc @ONLY)
install(FILES ${PROJECT_BINARY_DIR}/quickfix.pc DESTINATION lib/pkgconfig)
