# lib/common — shared core library (REQ §4)
add_library(instr_common STATIC
  src/timestamps.cpp
  src/json.cpp
  src/log.cpp
  src/alarm_codec.cpp
  src/crypto.cpp
  src/process.cpp
  src/filesystem.cpp
  src/atomic_file.cpp
  src/spool.cpp
  src/config.cpp
  src/command_table.cpp
  src/tar.cpp
  src/daemon_skeleton.cpp
  src/signature.cpp
)
target_include_directories(instr_common PUBLIC include)
# Threads::Threads is the portable way to get pthreads: glibc >= 2.34 has the
# symbols in libc, older toolchains (JetPack/L4T aarch64, older Yocto) keep
# them in libpthread. Every lib, daemon, tool and test links instr_common
# transitively, so this one line covers the whole tree.
target_link_libraries(instr_common PUBLIC yaml-cpp Threads::Threads)

# All cryptography (T-P31, INS-001..003) delegates to OpenSSL libcrypto:
# SHA-256, HMAC, X25519, ChaCha20(-Poly1305), HKDF and Ed25519 signature
# verification — no hand-written primitives. Required, not optional.
find_package(OpenSSL REQUIRED)
target_link_libraries(instr_common PUBLIC OpenSSL::Crypto)
target_compile_definitions(instr_common PUBLIC INSTR_HAVE_OPENSSL=1)
target_compile_features(instr_common PUBLIC cxx_std_17)
# Warning flags: GNU/Clang only, same guard as the top level.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(instr_common PRIVATE -Wall -Wextra -Wshadow)
endif()