# Source publication

Only the framework, public API, tooling, dependencies, tests and approved samples
belong in this repository. Application mods and their tests, configuration, translations
and documentation belong in separate repositories.

scripts/release/source-inventory.json records the exact reviewed files and SHA256 digests,
using normalized LF line endings. Assert-Source.py checks the complete Git file list against
that inventory and permits mod manifests only for the explicitly approved samples.
The release workflow performs this check before fetching dependencies or building.

A legitimate source change requires reviewing the changed files and updating the inventory.
The check detects unreviewed files or modifications; it does not replace human source review.
Local backups, build products, installations and credentials must remain outside the inventory.
Keep the repository private until publication is explicitly authorized.
