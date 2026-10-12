# Shared version catalogue

Edit `game_versions.json`, then run `python tools/generate_game_versions.py`.
The generated C# object array, C++ constexpr object array and CMake profile
list are tracked and checked by CMake / the native test runner. Hash, display
name, compatibility, reason, language and content rules have one source of truth.
Keep generated outputs with the catalogue edit in the same commit.

`requiredFiles` must all exist. A rejection rule matches only when **all**
`rejectFiles` and `rejectDirectories` exist; empty rejection lists mean no rule.
This retains the known mixed German retail/TU1 guard without locale-specific
branches. Adding or denying a version requires a catalogue entry, not changes
to the classification, content-validation, default-language or test loops.

A compatible hash is not sufficient evidence of compatible guest code. New
entries must have their generated mappings/payload validated and belong to
the tested `codeGroup` before enabling compatibility. Another code group needs
its own compilation profile. Single-edition builds still reject other profiles.
Russian support mentioned in upstream issue #6 is not asserted by this change.

The optional metadata fields document dump evidence; they are not read at
runtime. No game material is embedded in this catalogue.
