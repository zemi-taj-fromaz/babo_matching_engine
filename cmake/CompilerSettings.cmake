# CompilerSettings.cmake
# All compiler / optimization policy for the project, in one place.
# Included once from the top-level CMakeLists after project() (so the compiler
# is already detected) and before the add_subdirectory() calls (so every target
# inherits the flags).

# --- Aggressive Release optimizations (gcc / clang, GNU driver) --------------
# Release already implies "-O3 -DNDEBUG" for these compilers; add native ISA
# tuning and loop unrolling. MSVC / clang-cl take /O2-style flags and are left
# untouched. NOTE: deliberately no -ffast-math — it would break the workload
# generator's floating-point determinism and thus the correctness references.
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT MSVC)
    string(APPEND CMAKE_C_FLAGS_RELEASE   " -O3 -march=native -funroll-loops")
    string(APPEND CMAKE_CXX_FLAGS_RELEASE " -O3 -march=native -funroll-loops")
endif ()

# --- Link-time optimization for Release --------------------------------------
include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR)
if (IPO_SUPPORTED)
    message(STATUS "IPO / LTO enabled for Release")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
else ()
    message(STATUS "IPO / LTO not supported: <${IPO_ERROR}>")
endif ()

# --- MinGW: self-contained binaries ------------------------------------------
# Statically link the gcc/libstdc++/winpthread runtime so harness.exe and the
# adapter DLLs don't depend on MinGW runtime DLLs being on PATH. Without this,
# the binaries only run from inside CLion (which injects its bundled MinGW's bin
# onto PATH); with it they run from any shell and from the benchmark scripts.
# Safe here because the harness<->adapter boundary is a pure C ABI — no C++
# objects cross it, so each module carrying its own runtime copy is fine.
if (MINGW)
    add_link_options(-static-libgcc -static-libstdc++ -static)
endif ()

# --- Helper: pin a target to bit-stable floating-point -----------------------
# Forbids FMA contraction and LTO on a target, so aggressive Release flags can
# never shift a floating-point result by a ULP and silently change its output.
# Used for the workload generator to keep correctness references valid across
# optimization changes on an unchanged machine.
function(target_deterministic_fp target)
    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT MSVC)
        target_compile_options(${target} PRIVATE -ffp-contract=off)
    endif ()
    set_target_properties(${target} PROPERTIES
            INTERPROCEDURAL_OPTIMIZATION         OFF
            INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF)   # config-specific too
endfunction()
