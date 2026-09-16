# CMake development and indexing

Open the BriefcaseNative root directory in CLion as a CMake project.
`CMakeLists.txt` describes the targets, sources, dependencies and include
directories. Its role is similar to a `.csproj` file and its project references
in C#.

A file does not automatically receive a compilation context merely because it is
present in the directory. Each `.cpp` file must belong to a CMake target. Header
and `.inc` files are normally analyzed through the files that include them. DLLs
are binaries and must not be indexed as source files.

## CLion

The local Debug profile uses `cmake-build-debug`. The official scripts use the
Release `build` directory. These are separate CMake caches, so building with the
scripts does not reload a model already open in CLion.

After adding a target or changing dependencies:

1. Select **Tools → CMake → Reload CMake Project** in CLion, or press
   `Ctrl+Shift+A` and search for that action.
2. Wait for configuration and indexing to finish.
3. For later changes, enable automatic reload under
   **Settings → Build, Execution, Deployment → CMake**.

Invalidating every IDE cache is normally unnecessary. Fix configuration errors
before reloading.

`CMAKE_EXPORT_COMPILE_COMMANDS` is enabled. Every build directory contains a
generated `compile_commands.json` with the exact commands, definitions and include
paths used for each source file. Do not edit it.

## Mbed TLS and CMake

Mbed TLS 3.6.7 still declares CMake policy compatibility with 3.5.1. With CMake 4,
`cmake/AdminCrypto.cmake` sets `CMAKE_POLICY_VERSION_MINIMUM=3.10` only in the scope
that adds this dependency. The third-party source and global warnings remain
unchanged. BriefcaseNative itself still requires CMake 3.28.

References:

- https://www.jetbrains.com/help/clion/reloading-project.html
- https://cmake.org/cmake/help/latest/variable/CMAKE_POLICY_VERSION_MINIMUM.html
