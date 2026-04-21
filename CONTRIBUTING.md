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

* What the code is doing when behavior is not obvious from the statements alone.
* Why the change was made (bug class, platform/compiler/runtime constraint, or invariant being protected).

This is a hard requirement for future changes in this repository. Comment intent should be concise and high-signal, and should be updated whenever the related logic changes.

## Questions
If you're unsure about any aspect of contributing, don't hesitate to get in touch via any of our official communities linked in our [readme](README.md#community) or [GitHub Discussions](https://github.com/86Box/86Box/discussions).
