# GLib, for mcpp

GLib 2.82.5 as three mcpp packages, with `upstream/` untouched and everything
this fork adds under `mcpp/`.

```toml
[dependencies]
gnome.gobject = "2.82.5"   # GType, signals, properties, GValue
gnome.gmodule = "2.82.5"   # dynamic module loading
# gnome.glib arrives transitively — do NOT name it; see below
```

```cpp
// No extern "C": glib decorates its own headers with G_BEGIN_DECLS, and
// wrapping them breaks under libc++ (glib.h pulls <stdlib.h>, which libc++
// routes through <cstdlib> — templates inside an extern "C" block).
#include <glib-object.h>
```

## Why a fork

**Generators, not line count** — the same criterion that made cairo (104k
lines) a plain descriptor and libdisplay-info (2k) a fork. GLib has five:

| upstream | here |
|---|---|
| `tools/gen-visibility-macros.py versions-macros` | `gen_version_macros()` |
| `tools/gen-visibility-macros.py visibility-macros` ×3 | `gen_visibility()` |
| `configure_file` → `glibconfig.h` | `gen_glibconfig()` |
| `configure_file` → `gmoduleconf.h` | `gen_gmoduleconf()` |
| `gobject/glib-mkenums` (816 lines of Python) | `gen_enumtypes()` |
| `configure_file` → `config.h` | `gen_config()` |

**There is no `sh` and no `python` in this tree.** `build.mcpp` is a compiled
C++ program, so each generator is a function in it.

`glib-mkenums` is reproduced **for the one input this build points it at** —
`glib/gunicode.h`, four enums — not wholesale. It reads upstream's `.template`
files from the tree rather than hard-coding them, and exits non-zero if that
header stops yielding four enums, because a silent drop would produce a library
missing `g_unicode_script_get_type` and the failure would land in a consumer.

## Three build programs, one set of generators

`mcpp/common/generators.h` holds them; each member has a thirty-line
`build.mcpp` that calls the ones it needs and writes into ITS OWN `include/`.

⚠️ **Each member generating its own copy is the fix for a silent failure**, not
duplication for its own sake. gobject used to list
`../include/gobject/glib-enumtypes.c` — a file glib's build program produced.
With gobject AND gmodule both named by one consumer, mcpp resolved the two
`../glib` path dependencies to ONE package, so only one member's `include/` was
ever written, and the source entry pointing into the other simply matched
nothing. No "file not found": the object was absent from the ninja file
entirely, and it surfaced as

```
undefined reference to `g_unicode_script_get_type'
```

in a consumer that named both. **Naming a file another package's build program
produces is the mistake.** The generators are deterministic, so three copies
cannot disagree.

`gobject` and `gmodule` still depend on `glib` by workspace path — which also
means a consumer must NOT name `glib` itself:

```
error: dependency 'gnome.glib' is requested as both a version dep and a path dep
```

Name `gobject` and `gmodule`; `glib` arrives transitively.

## ⚠️ `include/` is first, and the ordering is load-bearing

glib's sources say `#include "config.h"`, and `config.h` is the most contested
file name in C. A build program's own `mcpp::include_dir()` is appended **after
every dependency's**, so a generated `config.h` placed there loses to any
dependency that ships one — measured in the wlroots fork at 59th of 59.
Manifest entries come first.

`include/.gitkeep` is committed because mcpp builds the compiler command line
**before** running the build program and silently drops an `include_dirs` entry
that does not exist yet.

## Three probe-macro traps this fork walked into

| | what happened |
|---|---|
| `#define HAVE_ISSETUGID 0` | autotools probes are `#ifdef`-tested, so **0 means yes**. glib called a BSD interface glibc does not have. Nine such macros are now absent rather than 0. |
| `GLIB_USING_SYSTEM_PRINTF` | the wrong one of two near-identical names. `USE_SYSTEM_PRINTF` (config.h) selects; the other (glibconfig.h) only reports. Setting only the second left glib calling its bundled gnulib printf, and none of `glib/gnulib` is compiled — a page of `undefined reference to _g_gnulib_snprintf`. |
| `STRERROR_R_CHAR_P` | glibc has two `strerror_r`. With `_GNU_SOURCE` it returns `char*`; the XSI one returns `int`. Choosing wrong is a compile error, which is the good case. |

And one in the other direction: `gmoduleconf.h.in` tests its values with
`#if (@X@)`, so there **0 is correct** and omission is a syntax error. The test
style decides, every time.

## What is not here

**`gio` is absent**, and the reason is specific rather than effort: two of its
six generators are `gdbus-codegen`, an 8,351-line Python program that turns
D-Bus interface XML into GObject skeletons. Seven gio sources include its
output and two more reference those, so it cannot be dropped without changing
what gio is.

That also means **pango is still blocked**: it uses `GListModel`, which lives
in gio.

## Layout

```
upstream/              glib 2.82.5, byte for byte (CI diffs it)
mcpp/common/
  generators.h         all six generators
  prelude.h            the four every member needs
mcpp/glib/
  mcpp.toml            `include` FIRST in include_dirs, deliberately
  build.mcpp           thirty lines: the shared four
  include/             GENERATED, not checked in (only .gitkeep)
  tests/glib.cpp
mcpp/gobject/          + gobject-visibility.h and glib-enumtypes.{h,c}
mcpp/gmodule/          + gmodule-visibility.h and gmoduleconf.h (flat)
```
