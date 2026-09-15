# Repository boundaries

The framework repository contains the host, backend, public API, tools, tests and samples.
Each application mod owns its sources, manifest, configuration, translations, tests and release.
Framework source, tests, documentation and packages must not depend on an application mod.

The versioned SDK archive contains the public C ABI, header-only C++ wrappers, JSON headers
and license, and the BriefcaseNativeSDK CMake package.
External projects use find_package(BriefcaseNativeSDK VERSION EXACT CONFIG REQUIRED)
and link Briefcase::ModApi and Briefcase::Json.

The framework updater preserves independently installed mods and their data.
Only the explicitly inventoried framework files belong to a framework update.

The SDK also provides Briefcase::DeceiveInc for typed game objects. Its adapter sources
are compiled into the consuming module and call only the public C ABI.
The SDK contains no host, backend or application mod implementation.
