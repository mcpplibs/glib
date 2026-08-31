# GLib, for mcpp

GLib 2.82.5 as four mcpp packages, with `upstream/` untouched and everything
this fork adds under `mcpp/`.

```toml
[dependencies]
gnome.gobject = "2.82.5.1"   # GType, signals, properties, GValue
gnome.gio     = "2.82.5.1"   # files, streams, sockets, D-Bus, GListModel
gnome.gmodule = "2.82.5.1"   # dynamic module loading
# gnome.glib arrives transitively — do NOT name it; see below
```

```cpp
// No extern "C": glib decorates its own headers with G_BEGIN_DECLS, and
// wrapping them breaks under libc++ (glib.h pulls <stdlib.h>, which libc++
// routes through <cstdlib> — templates inside an extern "C" block).
#include <glib-object.h>
#include <gio/gio.h>
```

## Versions

`2.82.5.1` is **upstream 2.82.5, fork revision 1**. The fourth component moves
when this fork changes and upstream does not.

Revision 1 adds `gnome.gio` and corrects a defect in `gnome.gobject 2.82.5`:
four generated macros were named `G_UNICODE_TYPE_TYPE`, `G_NORMALIZE_TYPE_MODE`
and so on, where upstream has `G_TYPE_UNICODE_TYPE`, `G_TYPE_NORMALIZE_MODE`.
Every *function* name was right, so it compiled, linked, and passed a test that
checked the function and the nick. **2.82.5 is not a usable base for anything
that names those macros; use 2.82.5.1.**

## Why a fork

**Generators, not line count** — the same criterion that made cairo (104k
lines) a plain descriptor and libdisplay-info (2k) a fork. GLib has six:

| upstream | here |
|---|---|
| `tools/gen-visibility-macros.py versions-macros` | `gen_version_macros()` |
| `tools/gen-visibility-macros.py visibility-macros` ×4 | `gen_visibility()` |
| `configure_file` → `glibconfig.h` | `gen_glibconfig()` |
| `configure_file` → `gmoduleconf.h` | `gen_gmoduleconf()` |
| `configure_file` → `gnetworking.h` | `gen_gnetworking()` |
| `configure_file` → `config.h` | `gen_config()` |
| `gobject/glib-mkenums` (816 lines of Python) | `write_enumtypes()` |
| `gio/gdbus-2.0/codegen` (8,351 lines of Python) | **checked in** — see below |

**There is no `sh` and no `python` in the build.** `build.mcpp` is a compiled
C++ program, so every generator above except the last is a function in it.

## ⭐ The one generator that is NOT reimplemented

`gdbus-codegen` turns D-Bus interface XML into GObject skeletons, proxies and
marshalling. Reproducing it would be a rewrite, not a reimplementation — and
that was once given as the reason gio was absent. It was the wrong reason,
because it conflated two different things:

    reproducing the generator     ≠     obtaining its output

The build does not need the generator. It needs **15,392 lines of C that are a
pure function of five XML files and the codegen version** — no target, no host,
no locale enters it. So `mcpp/tools/gengdbus.sh` produces them once, they live
in `mcpp/generated/`, and **CI regenerates and diffs**. That last part is not
optional: committed output has exactly one failure mode a build never sees — it
can stop matching its input, and it still compiles.

Same arrangement as `mcpplibs/wayland-protocols` and its 195 generated files.

The script runs **out of tree**, which is not tidiness: `codegen/config.py` is
itself a `configure_file`, and CPython writes `__pycache__/` on import. Both
would land in `upstream/` and fail the "release tarball, unmodified" job.

## `glib-mkenums`, at two very different scales

| | enums | annotations |
|---|---|---|
| `gobject` | 4, from `glib/gunicode.h` | none |
| `gio` | **82**, across the 152 headers `gio.h` reaches (3 of which have any) | `/*< flags >*/` ×7, `/*< nick=… >*/` ×17, `/*< prefix=… >*/` ×1 |

What changes at gio's scale is not the loop but the annotations — and one trap
in each direction:

- gio writes `/*< private >*/`, `/*< public >*/` and `/*< protected >*/` **144
  times**. Those are GTK-DOC annotations for *struct members* and mean nothing
  to mkenums. A scanner that read every `/*< … >*/` would silently drop
  enumerators.
- `GConverterFlags` has no `/*< flags >*/` **despite the name**, so upstream
  registers it with `g_enum_register_static`. A scanner that guessed from the
  type name would disagree with upstream's ABI.

⭐ **And the prefix rule, which shipped wrong once.** mkenums has *two*
prefixes: `enum_prefix`, taken from the ENUMERATORS, drives the nicks; and
`@ENUMPREFIX@`, taken from the TYPE NAME, drives the macro. Conflating them
gives `G_UNICODE_TYPE_TYPE` instead of `G_TYPE_UNICODE_TYPE` — which compiles,
links, and passes any test that checks the function or the nick. CI now checks
the macro, the nick, and enum-vs-flags separately, because they fail
independently.

## Four build programs, one set of generators

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
produces is the mistake.** The generators are deterministic, so four copies
cannot disagree.

`gobject`, `gmodule` and `gio` depend on `glib` by workspace path — which also
means a consumer must NOT name `glib` itself:

```
error: dependency 'gnome.glib' is requested as both a version dep and a path dep
```

Name what you use; `glib` arrives transitively.

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

## What gio does and does not carry

gio is assembled from four kinds of input, and three of them degrade *silently*
if left out — so each is checked by name in `mcpp/gio/tests/gio.cpp`:

| | absent, you get |
|---|---|
| `gio/xdgmime/` | `g_content_type_guess` answers `application/octet-stream` for everything |
| `gio/inotify/` | the file monitor quietly falls back to polling |
| `subprojects/gvdb/` | GResource and GSettings' schema reader are never reachable |
| `mcpp/generated/` | it still compiles; the portal calls fail only inside a Flatpak sandbox |

**Not built:** the ten tools (`gio-tool`, `glib-compile-schemas`,
`gdbus-tool`, …). Each provides `main`, which collides with the consumer's —
the rule every package in this index follows. `gconstructor_as_data.h` goes with
them, since it exists only for `glib-compile-resources`.

**No external dependency but zlib.** Upstream's meson names exactly one other,
`libelf`, and it is used by `gresource-tool.c` — an `executable()`, not the
library. There is therefore no libelf feature, because there is nothing for it
to switch. `libmount`, `selinux` and `sysprof` are `auto` upstream and absent
here; each is `#ifdef`-tested, so each is *absent* rather than 0, and gio
degrades the way upstream intends.

## Layout

```
upstream/              glib 2.82.5, byte for byte (CI diffs it)
mcpp/common/
  generators.h         the generators
  prelude.h            the four every member needs, plus gio's three
mcpp/generated/        gdbus-codegen output, 15,392 lines (CI regenerates+diffs)
mcpp/tools/
  gengdbus.sh          the maintainer script that produces the above
mcpp/glib/
  mcpp.toml            `include` FIRST in include_dirs, deliberately
  build.mcpp           thirty lines: the shared four
  include/             GENERATED, not checked in (only .gitkeep)
  tests/glib.cpp
mcpp/gobject/          + gobject-visibility.h and glib-enumtypes.{h,c}
mcpp/gmodule/          + gmodule-visibility.h and gmoduleconf.h (flat)
mcpp/gio/              + gio-visibility.h, gnetworking.h, gioenumtypes.{h,c}
```
