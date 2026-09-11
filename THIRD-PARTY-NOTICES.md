# Third-party notices

Dependencies retain their own copyrights and license terms. Their notices are not
replaced by Piggyback's GPL license. The pinned dependency versions are recorded
in the build configuration; vcpkg installs their license texts in
`vcpkg_installed/<triplet>/share/<package>/copyright`.

A migration to alandtse/CommonLibSSE-NG 7.5.1 is being prepared separately.
That library is GPL-3.0-or-later with upstream modding and linking exceptions;
see its COPYING.txt, EXCEPTIONS.md and licenses directory:
https://github.com/alandtse/CommonLibSSE-NG/tree/v7.5.1

The license update does not itself migrate dependencies or establish compatibility
with a new Skyrim runtime. Before distributing a new binary, preserve applicable
third-party notices and provide the corresponding source and build instructions
required by its licenses.
