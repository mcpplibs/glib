#!/bin/sh
#
# Regenerate mcpp/generated/ from upstream's D-Bus interface XML.
#
# WHY THE OUTPUT IS CHECKED IN
#
# gio has six generators. Five of them are text transformations that
# mcpp/common/generators.h reproduces in C++, so the build needs no
# interpreter. The sixth is `gdbus-codegen`: 8,351 lines of Python that turn a
# D-Bus interface description into GObject skeletons, proxies and marshalling.
# Reproducing THAT in build.mcpp would be a rewrite, not a reimplementation.
#
# But the build does not need the generator — it needs the OUTPUT, and the
# output is a pure function of the XML and the codegen version: no target, no
# host, no locale enters it. So it is produced once here, checked in, and CI
# regenerates and diffs. Same arrangement as mcpplibs/wayland-protocols, and
# for the same reason: precomputable output belongs in the repo rather than in
# every consumer's build.
#
# THE ARGUMENTS ARE UPSTREAM'S, COPIED FROM gio/meson.build:238 AND :254.
# --interface-prefix and --c-namespace decide every generated symbol name, so a
# difference here is a difference in gio's ABI.
#
# ⚠️ IT RUNS OUT OF TREE, AND THAT IS NOT TIDINESS. Two things want to write
# into upstream/ if this is run in place:
#
#     codegen/config.py     a configure_file, so it does not exist in the
#                           tarball — it has to be produced from config.py.in
#     codegen/__pycache__/  CPython writes it on import, unasked
#
# `upstream/ is the release tarball, unmodified` is a CI job, and either of
# those would fail it. So the codegen package is COPIED to a scratch tree and
# run from there; upstream/ is only ever read.
#
# Usage: mcpp/tools/gengdbus.sh [python3]
set -eu

PY="${1:-python3}"
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
GIO="$ROOT/upstream/gio"
OUT="$ROOT/mcpp/generated"

command -v "$PY" >/dev/null 2>&1 || { echo "not a python: $PY" >&2; exit 1; }
"$PY" --version

# The scratch tree has to mirror `<srcdir>/gio/gdbus-2.0`, because that is the
# layout gdbus-codegen.in derives from UNINSTALLED_GLIB_SRCDIR.
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/gio/gdbus-2.0"
cp -R "$GIO/gdbus-2.0/codegen" "$WORK/gio/gdbus-2.0/codegen"
rm -rf "$WORK/gio/gdbus-2.0/codegen/__pycache__"

CG="$WORK/gio/gdbus-2.0/codegen"

# codegen/config.py is itself a configure_file. Its three values appear in the
# generated files' header comments and in --version, nowhere else.
sed -e 's/@VERSION@/2.82.5/' -e 's/@MAJOR_VERSION@/2/' -e 's/@MINOR_VERSION@/82/' \
    "$CG/config.py.in" > "$CG/config.py"

mkdir -p "$OUT"
export UNINSTALLED_GLIB_SRCDIR="$WORK"
export PYTHONDONTWRITEBYTECODE=1

# The XDG portal interfaces. gio uses these to reach the host from inside a
# Flatpak sandbox — gopenuriportal.c, gtrashportal.c, gproxyresolverportal.c
# and gdocumentportal.c all include the output.
"$PY" "$CG/gdbus-codegen.in" \
    --interface-prefix org.freedesktop.portal. \
    --output-directory "$OUT" \
    --generate-c-code xdp-dbus \
    --c-namespace GXdp \
    "$GIO/org.freedesktop.portal.Documents.xml" \
    "$GIO/org.freedesktop.portal.OpenURI.xml" \
    "$GIO/org.freedesktop.portal.ProxyResolver.xml" \
    "$GIO/org.freedesktop.portal.Trash.xml"

# The session-bus daemon interface, used by gdbusdaemon.c. Needed on every
# platform because gdbusprivate.c references it, not only on Windows where the
# daemon itself is used.
"$PY" "$CG/gdbus-codegen.in" \
    --interface-prefix org. \
    --output-directory "$OUT" \
    --generate-c-code gdbus-daemon-generated \
    --c-namespace _G \
    "$GIO/dbus-daemon.xml"

echo "regenerated $(ls "$OUT" | wc -l) file(s) -> mcpp/generated/"
wc -l "$OUT"/*.c "$OUT"/*.h
