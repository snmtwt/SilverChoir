# H5UIPluginQuickJS

This module compiles the official QuickJS-NG `0.15.1` amalgam as a private Unreal runtime dependency.

- Upstream: `https://github.com/quickjs-ng/quickjs`
- Release: `https://github.com/quickjs-ng/quickjs/releases/tag/v0.15.1`
- Archive SHA-256: `D4DBF9CBF7A855C790D3C4C468AC45B00371D56FD8AE26E1AAA1D336EFC589D8`
- License: MIT, retained at `Private/QuickJS/LICENSE.txt`

The amalgam has one Unreal/MSVC compatibility guard near the top of `quickjs-amalgam.c`: when MSVC reports `__STDC_NO_ATOMICS__`, the intentionally failing C11 `<stdatomic.h>` include is skipped. QuickJS-NG already disables Atomics and SharedArrayBuffer for that configuration. No JavaScript behavior is otherwise changed.
