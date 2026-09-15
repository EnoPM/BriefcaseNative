# Client mod configuration

A mod registers its JSON configuration schema through the public API.
The menu renders the schema using labels, categories, translation keys and supported controls.
The implementation of a mod is not needed to edit its settings.

For live application, obtain briefcase.client.settings through get_service(version 1).
snapshot reads the saved revision into a caller-owned buffer. Start with revision zero.
An empty buffer means unchanged; BC_LIMIT preserves the caller's revision.
Apply values on the appropriate thread and acknowledge the revision afterwards.
A stale acknowledgment cannot overwrite a newer saved revision.
Without this optional service, saved settings take effect on restart.

A schema field with control="keybind" uses the framework's key selector.
Escape cancels capture; keys already held must be released first.
Leaving the page, closing the menu or losing focus cancels the selector.
Mods do not own an ImGui context or input hooks.

The menu is tested with a synthetic configuration: text visibility, opacity and a shortcut.
It has no dependency on an application mod.
