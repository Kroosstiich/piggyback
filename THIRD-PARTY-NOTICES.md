# Third-party notices

Piggyback 1.1.1 targets CommonLibSSE-NG 7.5.1 by alandtse and its contributors,
commit `bedcb1e05418baba7b316a650b6180c2dd6007a8`.

- Source: https://github.com/alandtse/CommonLibSSE-NG/tree/v7.5.1
- License: GPL-3.0-or-later with the upstream Modding Exception and GPL-3.0
  Linking Exception (with Corresponding Source).
- Upstream notices: `COPYING.txt`, `EXCEPTIONS.md`, and `licenses/` in that source tree.

Those exceptions apply to the upstream code as specified by its authors.
Piggyback's own code is GPL-3.0-or-later.

The build also uses dependencies selected by the pinned vcpkg baseline.
Their copyright and license files are installed under
`vcpkg_installed/<triplet>/share/<package>/copyright`.
Preserve applicable notices and provide corresponding source when distributing
a binary, including the exact CommonLibSSE-NG revision and build dependencies.

The release includes the applicable notices in licenses/ and is accompanied by
Piggyback-1.1.1-source.zip, containing corresponding source and build instructions.
The source archive also includes the exact vcpkg port recipes and dependency
archives verified against their recorded SHA-512 values.

MinHook/hde64 1.3.4 (c3fcafdc10146beb5919319d0683e44e3c30d537)
and OpenVR (60eb187801956ad277f1cae6680e3a410ee0873b) retain their upstream
licenses, included under licenses/MinHook and licenses/OpenVR.
