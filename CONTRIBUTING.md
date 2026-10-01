# Contributing to StormByte-Network

Issues and pull requests belong on **this** repository only. Fork and open a pull request against the integration branch named by the maintainers.

Coding rules for this repository are in [CODING_STYLE.md](CODING_STYLE.md). Read it before changing code.

By submitting a contribution you assign copyright in that contribution to the copyright holder of this repository (David C. Manuelda). The dual license in [LICENSE](LICENSE) can then apply to it.

Send only code you wrote and are free to assign. Do not send code owned by an employer, a third party, or another project unless you already have the right to assign that copyright here. Each contributor is responsible for that clearance.

New `.hxx` / `.h` / `.hpp` / `.cxx` / `.cpp` / `.cc` / `.c` files must start with the license header used in this repository, unchanged. Do not invent a shorter banner. CMake, Markdown and other non-C++ files do not take that header.

## Pull requests

- Keep the diff focused on one topic.
- Follow [CODING_STYLE.md](CODING_STYLE.md), including visibility, Doxygen, commits, and test requirements.
- Add or extend tests when behavior changes.
- Do not add files under `thirdparty/` for Network-only changes.
- Do not include generated build trees or documentation output.
- Maintainers may ask you to rebase on the current integration branch.
