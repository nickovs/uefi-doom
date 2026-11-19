# EDK2 MdePkg headers (vendored subset)

- Upstream: https://github.com/tianocore/edk2
- Tag: `edk2-stable202608`, commit `2970e5699ba6267f3384ffab20f96647578aebc8`
- Licence: BSD-2-Clause-Patent (`License.txt`), GPLv2-compatible.
- Contents: `MdePkg/Include`, pruned to the headers our build actually includes
  (`tools/prune_edk2_headers.py`). Headers are unmodified.
- Used for type, protocol and table definitions only; no EDK2 libraries are linked.
