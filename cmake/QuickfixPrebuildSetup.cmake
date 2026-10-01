# Needed to build examples

# Every top-level header in src/C++ -- the set install() in src/C++/CMakeLists.txt ships -- globbed
# rather than listed, so a new header cannot be left out of include/quickfix/, the quickfix target's
# INTERFACE include directory. config.h and QuickFIXVersion.h are written there by configure_file,
# and Allocator.h by its own target below.
file(GLOB QUICKFIX_TOP_LEVEL_HEADERS CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/C++/*.h)
list(FILTER QUICKFIX_TOP_LEVEL_HEADERS EXCLUDE REGEX "/(config|Allocator)\\.h$")

add_custom_target(QUICKFIX_HEADERS_COPY ALL 
COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_SOURCE_DIR}/include/
COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_SOURCE_DIR}/include/quickfix
COMMAND ${CMAKE_COMMAND} -E copy_if_different ${QUICKFIX_TOP_LEVEL_HEADERS} ${PROJECT_SOURCE_DIR}/include/quickfix/

COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_SOURCE_DIR}/include/quickfix/fix40
COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/src/C++/fix40 ${PROJECT_SOURCE_DIR}/include/quickfix/fix40/
COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_SOURCE_DIR}/include/quickfix/fix41
COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/src/C++/fix41 ${PROJECT_SOURCE_DIR}/include/quickfix/fix41/
COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_SOURCE_DIR}/include/quickfix/fix42
COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/src/C++/fix42 ${PROJECT_SOURCE_DIR}/include/quickfix/fix42/
COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_SOURCE_DIR}/include/quickfix/fix43
COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/src/C++/fix43 ${PROJECT_SOURCE_DIR}/include/quickfix/fix43/
COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_SOURCE_DIR}/include/quickfix/fix44
COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/src/C++/fix44 ${PROJECT_SOURCE_DIR}/include/quickfix/fix44/
COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_SOURCE_DIR}/include/quickfix/fix50
COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/src/C++/fix50 ${PROJECT_SOURCE_DIR}/include/quickfix/fix50/
COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_SOURCE_DIR}/include/quickfix/fix50sp1
COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/src/C++/fix50sp1 ${PROJECT_SOURCE_DIR}/include/quickfix/fix50sp1/
COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_SOURCE_DIR}/include/quickfix/fix50sp2
COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/src/C++/fix50sp2 ${PROJECT_SOURCE_DIR}/include/quickfix/fix50sp2/
COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_SOURCE_DIR}/include/quickfix/fixt11
COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/src/C++/fixt11 ${PROJECT_SOURCE_DIR}/include/quickfix/fixt11/
)

if (EXISTS ${PROJECT_SOURCE_DIR}/src/C++/Allocator.h)
add_custom_target(QUICKFIX_ALLOCATOR_HEADERS_COPY ALL
COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROJECT_SOURCE_DIR}/src/C++/Allocator.h ${PROJECT_SOURCE_DIR}/include/quickfix/Allocator.h
)
endif()
