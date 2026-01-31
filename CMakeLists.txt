cmake_minimum_required (VERSION 3.21..3.31)
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()
if(NOT CMAKE_VERSION VERSION_LESS 3.30 AND NOT POLICY CMP0167)
  cmake_policy(SET CMP0167 OLD)
endif()

set (opendht_VERSION_MAJOR 3)
set (opendht_VERSION_MINOR 4.0)
set (opendht_VERSION ${opendht_VERSION_MAJOR}.${opendht_VERSION_MINOR})
set (PACKAGE_VERSION ${opendht_VERSION})
set (VERSION "${opendht_VERSION}")
project (opendht
    VERSION ${opendht_VERSION}
    DESCRIPTION "OpenDHT: A lightweight C++17 Distributed Hash Table library."
    HOMEPAGE_URL "https://opendht.net/"
)

include(CMakePackageConfigHelpers)
include(CMakeDependentOption)
include(CheckIncludeFileCXX)
if(NOT MSVC)
    include(FindPkgConfig)
endif()
include(cmake/CheckAtomic.cmake)
include(CTest)

# Options
option (BUILD_SHARED_LIBS "Build shared library" ON)
option (OPENDHT_PYTHON "Build Python bindings" OFF)
option (OPENDHT_TOOLS "Build DHT tools" ON)
option (OPENDHT_SYSTEMD "Install systemd module" OFF)
option (OPENDHT_SYSTEMD_UNIT_FILE_LOCATION "Where to install systemd unit file")
option (OPENDHT_SANITIZE "Build with address sanitizer and stack protector" OFF)
option (OPENDHT_PROXY_SERVER "Enable DHT proxy server, use Restinio and jsoncpp" OFF)
option (OPENDHT_PUSH_NOTIFICATIONS "Enable push notifications support" OFF)
option (OPENDHT_PROXY_SERVER_IDENTITY "Allow clients to use the node identity" OFF)
option (OPENDHT_PROXY_CLIENT "Enable DHT proxy client, use Restinio and jsoncpp" OFF)
option (OPENDHT_PROXY_OPENSSL "Build DHT proxy with OpenSSL" ON)
CMAKE_DEPENDENT_OPTION(OPENDHT_HTTP "Build embedded http(s) client" OFF "NOT OPENDHT_PROXY_SERVER;NOT OPENDHT_PROXY_CLIENT" ON)
option (OPENDHT_PEER_DISCOVERY "Enable multicast peer discovery" ON)
option (OPENDHT_IO_URING "Use io_uring if available on the system (Linux only)" OFF)
option (OPENDHT_INDEX "Build DHT indexing feature" OFF)
option (OPENDHT_TESTS_NETWORK "Enable unit tests that require network access" ON)
option (OPENDHT_C "Build C bindings" OFF)
option (OPENDHT_CPACK "Add CPack support" OFF)
option (OPENDHT_SKIP_INSTALL_DEPS "Do not install runtime dependencies" OFF)

# Build flags
set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED on)
if(MSVC AND NOT BUILD_SHARED_LIBS AND NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if (OPENDHT_C)
    set (CMAKE_C_STANDARD 11)
    set (CMAKE_C_STANDARD_REQUIRED ON)
    if (MSVC)
        set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /experimental:c11atomics")
        set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /experimental:c11atomics")
    endif()
endif()

if (NOT HAVE_CXX_ATOMICS_WITHOUT_LIB
        # For ARM EABI (armel), little-endian MIPS (mipsel), etc.
        OR NOT HAVE_CXX_ATOMICS64_WITHOUT_LIB)
    link_libraries (atomic)
endif ()

if(WIN32) # https://stackoverflow.com/a/40217291/9560245
    macro(get_WIN32_WINNT version)
        if(CMAKE_SYSTEM_VERSION)
            set(ver ${CMAKE_SYSTEM_VERSION})
            string(REGEX MATCH "^([0-9]+).([0-9])" ver ${ver})
            string(REGEX MATCH "^([0-9]+)" verMajor ${ver})
            # Check for Windows 10, b/c we'll need to convert to hex 'A'.
            if("${verMajor}" MATCHES "10")
                set(verMajor "A")
                string(REGEX REPLACE "^([0-9]+)" ${verMajor} ver ${ver})
            endif()
            # Remove all remaining '.' characters.
            string(REPLACE "." "" ver ${ver})
            # Prepend each digit with a zero.
            string(REGEX REPLACE "([0-9A-Z])" "0\\1" ver ${ver})
            set(${version} "0x${ver}")
        endif()
    endmacro()

    get_WIN32_WINNT(WINNT_VERSION)
    # Prevents MinGW from problems linking Windows networking and IPC functions on Windows 10/11
    # if _WIN32_WINNT is not defined correctly
    message(STATUS "Windows NT version detection: ${WINNT_VERSION}")
    if(NOT MSVC)
        add_definitions(-D_WIN32_WINNT=${WINNT_VERSION} -DWINVER=${WINNT_VERSION})
    endif()
endif()

if (WIN32)
    set(MAP_IMPORTED_CONFIG_RELWITHDEBINFO RELEASE) # MSVC CMake cache workaround
endif ()

list (APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

if (NOT MSVC)
    # Generalized UNIX/MSYS2 build pipeline
    set (THREADS_PREFER_PTHREAD_FLAG TRUE)
    find_package (Threads)
    find_package (PkgConfig REQUIRED)
    # GNUTLS
    pkg_search_module (GnuTLS REQUIRED IMPORTED_TARGET gnutls)
    # libnettle
    pkg_search_module (Nettle REQUIRED IMPORTED_TARGET nettle)
    # libargon2
    pkg_search_module (argon2 REQUIRED IMPORTED_TARGET libargon2)
    set(argon2_lib ", libargon2")
    # jsoncpp
    pkg_search_module (Jsoncpp REQUIRED IMPORTED_TARGET jsoncpp)
    include_directories(${Jsoncpp_INCLUDE_DIRS})
    #  libfmt
    find_package(fmt CONFIG REQUIRED)
    # libreadline
    if (OPENDHT_TOOLS AND NOT APPLE)   
        pkg_search_module(readline REQUIRED IMPORTED_TARGET readline)
        message(STATUS "libreadline: version ${readline_VERSION}")
        set(READLINE_VERSION ${readline_VERSION})
        if(READLINE_VERSION VERSION_LESS 6)
            message(SEND_ERROR "libreadline: required version 6.0 or later")
        endif()
    endif ()
    # ASIO
    if (OPENDHT_HTTP OR OPENDHT_PEER_DISCOVERY)
        pkg_search_module(asio REQUIRED IMPORTED_TARGET asio)
        set(ASIO_INCLUDE_DIR ${asio_INCLUDE_DIRS})
        message(STATUS "Found ASIO: ${ASIO_INCLUDE_DIR}")
    endif ()
    # llhttp
    if (OPENDHT_HTTP)
        find_package(llhttp CONFIG REQUIRED)
        if(TARGET llhttp::llhttp_static)
            add_library(llhttp::llhttp ALIAS llhttp::llhttp_static)
        else()
            add_library(llhttp::llhttp ALIAS llhttp::llhttp_shared)
        endif()
    else ()
        set(OPENDHT_PROXY_OPENSSL OFF)
    endif ()
    if (OPENDHT_PROXY_OPENSSL)
        find_package(OpenSSL REQUIRED)
        set(openssl_lib ", openssl")
    endif()

    find_package(msgpackc-cxx QUIET CONFIG NAMES msgpackc-cxx msgpack)
    if(msgpackc-cxx_FOUND)
        add_library(msgpack-cxx ALIAS msgpackc-cxx)
    else()
        find_package(msgpack-cxx CONFIG REQUIRED)
    endif()

    # restinio: does not provide pkg-config PC file
    if(OPENDHT_HTTP)
        find_package(restinio REQUIRED)
    endif()
    # System-specific pipeline setup
    if(APPLE)
        if (OPENDHT_TOOLS)
            find_package (Readline 6 REQUIRED)
        endif ()
    endif()
else () # MSVC
    include_directories(src/compat/msvc)
    include_directories(src/compat/win32)

    find_package(msgpack-cxx CONFIG REQUIRED)
    get_target_property(MSGPACK_INCLUDEDIR msgpack-cxx INTERFACE_INCLUDE_DIRECTORIES)
    include_directories(${MSGPACK_INCLUDEDIR})

    if(OPENDHT_TOOLS)
        pkg_search_module(readline REQUIRED IMPORTED_TARGET readline)
        include_directories(${readline_INCLUDEDIR})
        link_directories(${readline_LIBDIR})
    endif()

    if (OPENDHT_HTTP OR OPENDHT_PEER_DISCOVERY)
        find_package(asio CONFIG REQUIRED)
        find_path(ASIO_INCLUDE_DIR asio.hpp REQUIRED HINTS ${ASIO_INCLUDE_DIRS}/asio/include)
        message(STATUS "Found ASIO ${ASIO_INCLUDE_DIR}")
        include_directories(${ASIO_INCLUDE_DIR})
        add_definitions(-DASIO_STANDALONE) 
    else()
        message(STATUS "ASIO not required")
    endif ()

    if (OPENDHT_HTTP)
        find_package(restinio CONFIG REQUIRED)
        find_package(llhttp CONFIG REQUIRED)
        set(http_lib "-lllhttp")

        if(TARGET llhttp::llhttp_static)
            add_library(llhttp::llhttp ALIAS llhttp::llhttp_static)
        else()
            add_library(llhttp::llhttp ALIAS llhttp::llhttp_shared)
        endif()

        if (OPENDHT_PROXY_OPENSSL)
            find_package(OpenSSL REQUIRED)
            set(openssl_lib ", openssl")
        endif()
    else()
        set(OPENDHT_PROXY_OPENSSL OFF)
    endif()

    find_package(fmt CONFIG REQUIRED)
    find_package (PkgConfig REQUIRED)

    pkg_search_module(GnuTLS REQUIRED IMPORTED_TARGET GnuTLS)
    include_directories(${GnuTLS_INCLUDEDIR})
    link_directories(${GnuTLS_LIBDIR})

    pkg_search_module(Nettle REQUIRED IMPORTED_TARGET Nettle)
    include_directories(${Nettle_INCLUDEDIR})
    link_directories(${Nettle_LIBDIR})

    pkg_search_module(argon2 REQUIRED IMPORTED_TARGET libargon2)
    include_directories(${argon2_INCLUDEDIR})
    link_directories(${argon2_LIBDIR})
    set(argon2_lib ", libargon2")

    pkg_search_module(Jsoncpp REQUIRED IMPORTED_TARGET jsoncpp)
    include_directories(${Jsoncpp_INCLUDEDIR})
    link_directories(${Jsoncpp_LIBDIR})
endif ()

# Definitions
add_definitions(-DOPENDHT_JSONCPP)
set(jsoncpp_lib ", jsoncpp")
list (APPEND opendht_SOURCES
    src/base64.h
    src/base64.cpp
)

if (OPENDHT_HTTP OR OPENDHT_PEER_DISCOVERY)
    add_definitions(-DASIO_STANDALONE)
    if (OPENDHT_IO_URING AND UNIX AND NOT APPLE)
        pkg_search_module(liburing IMPORTED_TARGET liburing)
    endif ()
endif()

if (NOT MSVC)
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-attributes -Wno-return-type -Wno-deprecated -Wno-deprecated-declarations -Wno-unknown-pragmas -Wall -Wextra -Wnon-virtual-dtor -pedantic-errors -fvisibility=hidden")
    if (OPENDHT_SANITIZE)
        set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fstack-protector-strong")
        set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fstack-protector-strong")
    endif ()
else ()
    add_definitions(-D_WINSOCK_DEPRECATED_NO_WARNINGS
                    -D_CRT_SECURE_NO_WARNINGS
                    -DWIN32_LEAN_AND_MEAN
                    -DSTATIC_GETOPT)
    set(DISABLE_MSC_WARNINGS "/wd4101 /wd4244 /wd4267 /wd4273 /wd4804 /wd4834 /wd4996")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${DISABLE_MSC_WARNINGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP /bigobj /utf-8 /EHsc")
endif ()
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DMSGPACK_NO_BOOST -DMSGPACK_DISABLE_LEGACY_NIL -DMSGPACK_DISABLE_LEGACY_CONVERT")

add_definitions(-DPACKAGE_VERSION="${opendht_VERSION}")

if (ASIO_INCLUDE_DIR)
    include_directories (SYSTEM "${ASIO_INCLUDE_DIR}")
endif ()
if (Restinio_INCLUDE_DIR)
    include_directories (SYSTEM "${Restinio_INCLUDE_DIR}")
endif ()
include_directories (
    ./
    include/
    include/opendht/
    ${CMAKE_CURRENT_BINARY_DIR}/include/
)

# Install dirs
include (GNUInstallDirs)
set (prefix ${CMAKE_INSTALL_PREFIX})
set (exec_prefix "\${prefix}")
set (libdir "${CMAKE_INSTALL_FULL_LIBDIR}")
set (includedir "${CMAKE_INSTALL_FULL_INCLUDEDIR}")
set (bindir "${CMAKE_INSTALL_FULL_BINDIR}")
set (sysconfdir "${CMAKE_INSTALL_FULL_SYSCONFDIR}")
set (top_srcdir "${CMAKE_CURRENT_SOURCE_DIR}")

# Sources
list (APPEND opendht_SOURCES
    src/utils.cpp
    src/crypto.cpp
    src/default_types.cpp
    src/node.cpp
    src/value.cpp
    src/dht.cpp
    src/op_cache.cpp
    src/storage.h
    src/listener.h
    src/search.h
    src/value_cache.h
    src/op_cache.h
    src/net.h
    src/parsed_message.h
    src/request.h
    src/callbacks.cpp
    src/routing_table.cpp
    src/node_cache.cpp
    src/network_engine.cpp
    src/securedht.cpp
    src/dhtrunner.cpp
    src/log.cpp
    src/network_utils.cpp
    src/thread_pool.cpp
)

list (APPEND opendht_HEADERS
    include/opendht.h
    include/opendht/def.h
    include/opendht/rng.h
    include/opendht/infohash.h
    include/opendht/utils.h
    include/opendht/sockaddr.h
    include/opendht/crypto.h
    include/opendht/default_types.h
    include/opendht/node.h
    include/opendht/value.h
    include/opendht/dht.h
    include/opendht/dht_interface.h
    include/opendht/callbacks.h
    include/opendht/routing_table.h
    include/opendht/node_cache.h
    include/opendht/network_engine.h
    include/opendht/scheduler.h
    include/opendht/rate_limiter.h
    include/opendht/securedht.h
    include/opendht/log.h
    include/opendht/logger.h
    include/opendht/thread_pool.h
    include/opendht/network_utils.h
    include/opendht.h
)

if (OPENDHT_PEER_DISCOVERY)
    list (APPEND opendht_SOURCES src/peer_discovery.cpp)
    list (APPEND opendht_HEADERS include/opendht/peer_discovery.h)
    add_definitions(-DOPENDHT_PEER_DISCOVERY)
endif()

if (OPENDHT_PYTHON)
    message("Indexation enabled since it is required for Python support")
    set(OPENDHT_INDEX ON)
endif()
if (OPENDHT_INDEX)
    list (APPEND opendht_SOURCES src/indexation/pht.cpp)
    list (APPEND opendht_HEADERS include/opendht/indexation/pht.h)
    add_definitions(-DOPENDHT_INDEXATION)
endif()

if (OPENDHT_PROXY_SERVER)
  add_definitions(-DOPENDHT_PROXY_SERVER)
  if (OPENDHT_PROXY_SERVER_IDENTITY)
    add_definitions(-DOPENDHT_PROXY_SERVER_IDENTITY)
  endif()
  list (APPEND opendht_HEADERS
    include/opendht/dht_proxy_server.h
  )
  list (APPEND opendht_SOURCES
    src/dht_proxy_server.cpp
  )
endif ()

if (OPENDHT_PROXY_CLIENT)
  add_definitions(-DOPENDHT_PROXY_CLIENT)
  list (APPEND opendht_HEADERS
    include/opendht/dht_proxy_client.h
  )
  list (APPEND opendht_SOURCES
    src/dht_proxy_client.cpp
  )
endif ()

if (OPENDHT_HTTP)
  if (OPENDHT_PUSH_NOTIFICATIONS)
    message(STATUS "Using push notification capability")
    add_definitions(-DOPENDHT_PUSH_NOTIFICATIONS)
  endif ()
  list (APPEND opendht_HEADERS
    include/opendht/proxy.h
    include/opendht/http.h
    src/compat/os_cert.h
  )
  list (APPEND opendht_SOURCES
    src/http.cpp
    src/compat/os_cert.cpp
  )
endif ()

if (MSVC)
    list (APPEND opendht_HEADERS src/compat/msvc/unistd.h)
endif ()

# Targets
add_library(opendht ${opendht_SOURCES} ${opendht_HEADERS})
target_include_directories(opendht
                           PUBLIC
                           "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
                           "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

set_target_properties(opendht PROPERTIES OUTPUT_NAME "opendht")

# Linker pipelines
if (MSVC)
    message(STATUS "Build pipeline: Windows/MSVC + vcpkg")
    target_link_libraries(opendht 
        PRIVATE
            PkgConfig::argon2
            PkgConfig::Nettle
        PUBLIC
            PkgConfig::GnuTLS
            msgpack-cxx
            PkgConfig::Jsoncpp
            fmt::fmt
            wsock32
            ws2_32
    )
    if (OPENDHT_TOOLS)
        target_link_libraries(opendht PUBLIC PkgConfig::readline)
    endif()
    if (OPENDHT_HTTP)
        target_link_libraries(opendht PUBLIC llhttp::llhttp restinio::restinio)
        if (OPENDHT_PROXY_OPENSSL)
            target_link_libraries(opendht PUBLIC OpenSSL::SSL OpenSSL::Crypto)
        endif()
    endif()
else()
    if (APPLE)
        message(STATUS "Build pipeline: Apple/Darwin")
        target_link_libraries(opendht PRIVATE "-framework CoreFoundation" "-framework Security")
    endif ()
    # Generalized UNIX/MSYS2 build pipeline

    target_link_libraries(opendht
        PRIVATE
            PkgConfig::argon2
            PkgConfig::Nettle
        PUBLIC
            ${CMAKE_THREAD_LIBS_INIT}
            PkgConfig::GnuTLS
            msgpack-cxx
            PkgConfig::Jsoncpp
            fmt::fmt
    )
    if(OPENDHT_TOOLS) 
        target_link_libraries(opendht PUBLIC PkgConfig::readline)
    endif()
    if(OPENDHT_HTTP OR OPENDHT_PEER_DISCOVERY)
        target_link_libraries(opendht PUBLIC PkgConfig::asio)
    endif()
    if (OPENDHT_PROXY_OPENSSL)
        target_link_libraries(opendht PUBLIC OpenSSL::SSL OpenSSL::Crypto)
    endif()

    if(OPENDHT_HTTP)
        target_link_libraries(opendht 
            PUBLIC # Required for linking tests
                llhttp::llhttp
                restinio::restinio
    )
    endif()
    if (OPENDHT_IO_URING AND liburing_FOUND)
        set(iouring_lib ", liburing")
        target_link_libraries(opendht PUBLIC PkgConfig::liburing)
        target_compile_definitions(opendht PUBLIC ASIO_HAS_IO_URING ASIO_DISABLE_EPOLL)
    endif()
    # System-specific linker pipelines
    if (WIN32 AND MINGW) # MSYS2
        message(STATUS "Build pipeline: Windows/MSYS2-MinGW")
        target_link_libraries(opendht PUBLIC wsock32 ws2_32)
        if(OPENDHT_PROXY_OPENSSL)
            target_link_libraries(opendht PUBLIC Crypt32)
        endif()
    else() # UNIX / LLVM
        message(STATUS "Build pipeline: UNIX generalized")
    endif()
endif()

if (BUILD_SHARED_LIBS)
    set_target_properties (opendht PROPERTIES IMPORT_SUFFIX "_import.lib")
    set_target_properties (opendht PROPERTIES SOVERSION ${opendht_VERSION_MAJOR} VERSION ${opendht_VERSION})
    target_compile_definitions(opendht PRIVATE OPENDHT_BUILD) 
    target_compile_definitions(opendht PUBLIC opendht_EXPORTS)
else ()
    target_compile_definitions(opendht PRIVATE GNUTLS_INTERNAL_BUILD)
endif ()

install (TARGETS opendht EXPORT opendht
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT library_files
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime_files
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT archive_files
    )

if (NOT OPENDHT_SKIP_INSTALL_DEPS)
    install (TARGETS opendht RUNTIME_DEPENDENCY_SET opendht_rdeps)

    if (WIN32)
        install(RUNTIME_DEPENDENCY_SET opendht_rdeps
            PRE_EXCLUDE_REGEXES
                "api-ms-win-.*\\.dll"
                "ext-ms-.*\\.dll"
                "d3d.*\\.dll"
                "dxgi.*\\.dll"
                "uxtheme\\.dll"
                "dwmapi\\.dll"
                "crypt32\\.dll"
                "bcrypt\\.dll"
                "ncrypt\\.dll"
                "sechost\\.dll"
                "user32\\.dll"
                "kernel32\\.dll"
                "gdi32\\.dll"
                "shell32\\.dll"
                "advapi32\\.dll"
                "ole32\\.dll"
                "oleaut32\\.dll"
                "shlwapi\\.dll"
                "comdlg32\\.dll"
                "winspool\\.drv"
                "mpr\\.dll"
                "version\\.dll"
                "ws2_32\\.dll" 
                "vcruntime.*\\.dll"
                "msvcp.*\\.dll"
                "wpax.*\\.dll"
                "azure.*\\.dll"
        )
    endif()
endif()

if (OPENDHT_C)
    add_library (opendht-c
        c/opendht.cpp
        c/opendht_c.h
    )
    target_compile_definitions(opendht-c PRIVATE OPENDHT_C_BUILD)
    target_link_libraries(opendht-c PRIVATE opendht)
    if(OPENDHT_TOOLS)
        target_link_libraries(opendht-c PUBLIC PkgConfig::readline)
    endif()
    if (BUILD_SHARED_LIBS)
        target_compile_definitions(opendht-c PRIVATE OPENDHT_C_BUILD)
        target_compile_definitions(opendht-c PUBLIC opendht_c_EXPORTS)
        set_target_properties (opendht-c PROPERTIES SOVERSION ${opendht_VERSION_MAJOR} VERSION ${opendht_VERSION})
    endif()
    install (TARGETS opendht-c DESTINATION ${CMAKE_INSTALL_LIBDIR} EXPORT opendht-c)

    # PkgConfig module
    configure_file (
        opendht-c.pc.in
        opendht-c.pc
        @ONLY
    )
    install (FILES ${CMAKE_CURRENT_BINARY_DIR}/opendht-c.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
    install (FILES c/opendht_c.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/opendht)
endif ()

if (OPENDHT_TOOLS)
    add_subdirectory(tools)
endif ()

if (OPENDHT_PYTHON)
    add_subdirectory(python)
endif ()

# CMake module
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/opendhtConfigVersion.cmake"
  VERSION ${opendht_VERSION}
  COMPATIBILITY AnyNewerVersion
)

# PkgConfig module
configure_file (
    opendht.pc.in
    opendht.pc
    @ONLY
)

# Install targets
configure_package_config_file(cmake/opendht-config.cmake.in opendht-config.cmake INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/opendht)
write_basic_package_version_file(opendht-config-version.cmake COMPATIBILITY SameMajorVersion)

set(TARGETS_FILE opendht-shared-targets.cmake)

if(NOT BUILD_SHARED_LIBS)
    set(TARGETS_FILE opendht-static-targets.cmake)
endif()

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT header_files)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/opendht.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig COMPONENT config_files)
install(EXPORT opendht DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/opendht FILE ${TARGETS_FILE} COMPONENT config_files)

install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/opendht-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/opendht-config-version.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/opendht COMPONENT config_files)
if(MSVC AND BUILD_SHARED_LIBS)
    install(FILES $<TARGET_PDB_FILE:opendht> CONFIGURATIONS "RelWithDebInfo" "Debug" DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT debug_files)
endif()

# Unit tests
if (BUILD_TESTING)
    if (NOT MSVC)
        pkg_search_module(Cppunit REQUIRED IMPORTED_TARGET cppunit)
        set(cppunit_LIBRARIES PkgConfig::Cppunit)
    else()
        find_package(CppUnit CONFIG REQUIRED)
        set(cppunit_LIBRARIES CppUnit)
    endif()
    # unit testing
    list (APPEND test_FILES
        tests/infohashtester.h
        tests/infohashtester.cpp
        tests/valuetester.h
        tests/valuetester.cpp
        tests/cryptotester.h
        tests/cryptotester.cpp
        tests/dhtrunnertester.h
        tests/dhtrunnertester.cpp
        tests/threadpooltester.h
        tests/threadpooltester.cpp
    )
    if (OPENDHT_TESTS_NETWORK)
        if (OPENDHT_PROXY_SERVER AND OPENDHT_PROXY_CLIENT)
            list (APPEND test_FILES
                tests/httptester.h
                tests/httptester.cpp
                tests/dhtproxytester.h
                tests/dhtproxytester.cpp
            )
        endif()
        if (OPENDHT_PEER_DISCOVERY)
            list (APPEND test_FILES
                tests/peerdiscoverytester.h
                tests/peerdiscoverytester.cpp
            )
        endif()
    endif()
    add_executable(opendht_unit_tests
        tests/tests_runner.cpp
        ${test_FILES}
    )
    target_link_libraries(opendht_unit_tests PRIVATE
        opendht
        ${CMAKE_THREAD_LIBS_INIT}
        ${cppunit_LIBRARIES}
    )
    if (OPENDHT_PROXY_OPENSSL)
        if (WIN32 AND MINGW) # MSYS2
            target_link_libraries(opendht_unit_tests PRIVATE OpenSSL::SSL OpenSSL::Crypto)
        elseif(NOT MSVC) # Clang on UNIX-pkgconfig
            target_link_libraries(opendht_unit_tests PRIVATE OpenSSL::SSL OpenSSL::Crypto)
        endif()
    endif()
    add_test(TEST opendht_unit_tests)
endif()

if (OPENDHT_CPACK)
    set(CPACK_PACKAGE_NAME "opendht")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_VENDOR "Savoir-faire Linux, Inc.")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "OpenDHT: A lightweight C++17 Distributed Hash Table library.")
    set(CPACK_PACKAGE_HOMEPAGE_URL "https://opendht.net/")
    set(CPACK_PACKAGE_CONTACT "support@savoirfairelinux.com") # Or a relevant contact/mailing list
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/COPYING.txt")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "OpenDHT")

    if(WIN32)
        # WiX specific settings
        set(CPACK_GENERATOR "WIX") # Can be overridden by -G on the command line
        set(CPACK_WIX_VERSION 4)
        set(CPACK_WIX_PRODUCT_GUID "5e2faa2b-a4f4-4431-ba15-ed94e732a949")
        set(CPACK_WIX_UPGRADE_GUID "cbff4222-9b41-48a2-94ea-6f8a228c5936")
        set(CPACK_WIX_PRODUCT_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/opendht_logo_512.ico")
        set(CPACK_WIX_UI_BANNER "${CMAKE_CURRENT_SOURCE_DIR}/resources/opendht_install_banner.bmp")
        set(CPACK_WIX_UI_DIALOG "${CMAKE_CURRENT_SOURCE_DIR}/resources/opendht_install_bg.bmp")
        set(CPACK_WIX_PROGRAM_MENU_FOLDER "${PROJECT_NAME}")
        set(CPACK_WIX_INSTALL_SCOPE "perMachine")
    endif()

    set(CPACK_PACKAGE_EXECUTABLES "dhtnode;DHT Node")
    set(CPACK_CREATE_DESKTOP_LINKS "dhtnode")
    if(NOT CPACK_PACKAGE_RELEASE)
        set(CPACK_PACKAGE_RELEASE "1")
    endif()
    set(CPACK_COMPONENTS_ALL_IN_ONE_PACKAGE ON)

    cmake_host_system_information(RESULT DISTR QUERY DISTRIB_ID)
    if(DISTR MATCHES "(debian)|(ubuntu)|(kali)|(astra)")
        cmake_host_system_information(RESULT DISTR_CODENAME QUERY DISTRIB_VERSION_CODENAME)
        set(CPACK_DEBIAN_PACKAGE_RELEASE "${CPACK_PACKAGE_RELEASE}+${DISTR_CODENAME}")
        set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
        set(CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS ON)
        set(CPACK_DEBIAN_DEBUGINFO_PACKAGE ON)
        set(CPACK_DEB_COMPONENT_INSTALL ON)
    else()
        cmake_host_system_information(RESULT DISTR_VERSION QUERY DISTRIB_VERSION_ID)
        if (DISTR MATCHES "fedora")
            set(CPACK_RPM_PACKAGE_RELEASE "${CPACK_PACKAGE_RELEASE}.fc${DISTR_VERSION}")
        elseif(DISTRO_ID MATCHES "rhel|centos|almalinux|rocky")
            string(REGEX REPLACE "\\." "" DISTR_VERSION "${DISTR_VERSION}")
            set(CPACK_RPM_PACKAGE_RELEASE "${CPACK_PACKAGE_RELEASE}.el${DISTR_VERSION}")
        else()
            set(CPACK_RPM_PACKAGE_RELEASE "${CPACK_PACKAGE_RELEASE}")
        endif()

        set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
        set(CPACK_RPM_PACKAGE_AUTOREQ "1")
        set(CPACK_RPM_COMPONENT_INSTALL ON)
        set(CPACK_RPM_BUILD_SOURCE_DIRS_PREFIX "/src/opendht")
    endif()

    string(TOLOWER "${PROJECT_NAME}-${PROJECT_VERSION}-${CPACK_PACKAGE_RELEASE}-${CMAKE_HOST_SYSTEM_NAME}-${CMAKE_HOST_SYSTEM_PROCESSOR}" CPACK_ARCHIVE_FILE_NAME)

    include(CPack)
endif()
