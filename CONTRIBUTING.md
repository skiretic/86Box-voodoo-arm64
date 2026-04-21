# Contribution guidelines
The 86Box project welcomes contributions from anyone, as long as some basic guidelines are followed.

## Emulated hardware
In order to accept new emulated hardware or a request thereof, the following criteria must be met:

* A ROM must be available and be added to [our ROM repository](https://github.com/86Box/roms)
* Documentation must be available or it must be feasible to reverse engineer with a reasonable amount of time and effort
* It must be feasible to implement with a reasonable amount of time and effort
* It has to fall inside the project's scope

## Code change documentation (required)
All non-trivial code changes must include developer-facing comments in the changed code that explain:

* What the changed block is doing.
* Why the change was made (bug class, platform/compiler/runtime constraint, or invariant being protected).

This is a hard requirement for future changes in this repository. Do not wait for code to be "non-obvious"; prefer explicit comments at change time so there is no ambiguity during review/execution. Comments can be trimmed or consolidated later once the behavior is fully stable, but they must be present when landing the change.

## Questions
If you're unsure about any aspect of contributing, don't hesitate to get in touch via any of our official communities linked in our [readme](README.md#community) or [GitHub Discussions](https://github.com/86Box/86Box/discussions).
