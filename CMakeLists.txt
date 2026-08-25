cmake_minimum_required(VERSION 3.16)

project(ftl
        VERSION 1.0.0
        DESCRIPTION "ftl: low-level C/C++ utilities + ftest testing framework"
        LANGUAGES C CXX)

include(GNUInstallDirs)

# ---------------------------------------------------------------- headers
set(FTL_HEADERS
        ftl/ftl.h
        ftl/ftl.hpp
        ftl/core/scope_guard.hpp
        ftl/core/error.hpp
        ftl/core/fd.hpp
        ftl/io/pipe.hpp
        ftl/io/capture.hpp
        ftl/io/temp.hpp
        ftl/process/subprocess.hpp
        ftl/thread/thread_count.hpp
        ftl/alloc/alloc_info.hpp)

set(FT_HEADERS
        ftest/framework.hpp
        ftest/capture.hpp
        ftest/files.hpp
        ftest/fdtrack.hpp
        ftest/alloc.hpp
        ftest/process.hpp
        ftest/guards.hpp
        ftest/matchers.hpp
        ftest/ftest.hpp)

# ---------------------------------------------------------------- targets
add_library(ftl INTERFACE)
target_include_directories(ftl INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(ftl INTERFACE cxx_std_17)
target_link_libraries(ftl INTERFACE Threads::Threads)

add_library(ftest INTERFACE)
target_include_directories(ftest INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(ftest INTERFACE cxx_std_17)
target_link_libraries(ftest INTERFACE ftl Threads::Threads)

# Threads for both consumers and the tests below.
find_package(Threads REQUIRED)

# ---------------------------------------------------------------- install
install(FILES ${FTL_HEADERS}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ftl)
install(FILES ${FT_HEADERS}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ftest)

install(TARGETS ftl ftest EXPORT ftlTargets)

install(EXPORT ftlTargets
        NAMESPACE ftl::
        FILE ftlTargets.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ftl)

# ---------------------------------------------------------------- self-test
enable_testing()

add_executable(test_library test_library.cpp)
target_link_libraries(test_library PRIVATE ftest)
add_test(NAME library-selftest COMMAND test_library)

add_executable(c_smoke_test c_smoke.c)
target_link_libraries(c_smoke_test PRIVATE m pthread)
add_test(NAME c-api-smoke COMMAND c_smoke_test)

if (FT_BUILD_PROJECT_TESTS)
        # Only valid when this directory is embedded in its parent project
        # (test_unit.cpp includes parent-project headers).
        add_executable(test_unit test_unit.cpp)
        target_link_libraries(test_unit PRIVATE ftest readline)
        add_test(NAME unit-series COMMAND test_unit)

        add_executable(test_cli test_cli.cpp)
        target_link_libraries(test_cli PRIVATE ftest)
        add_test(NAME cli-series COMMAND test_cli)
endif ()
