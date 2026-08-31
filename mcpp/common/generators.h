// The GLib generators, shared by all three members' build programs.
//
// ⚠️ EACH MEMBER GENERATES ITS OWN COPY, into its own `include/`. That is not
// duplication for its own sake — it is the fix for a failure that is silent:
//
//   gobject listed `../include/gobject/glib-enumtypes.c` in its sources, a
//   file glib's build program produced. With gobject AND gmodule both named by
//   one consumer, mcpp resolved the two `../glib` path dependencies to ONE
//   package, so only one member's `include/` was ever written — and the source
//   entry pointing into the other simply matched nothing. No "file not found";
//   the object was absent from the ninja file entirely, and the failure
//   surfaced as
//
//     undefined reference to `g_unicode_script_get_type'
//
//   in a consumer that named both. Naming a file another package's build
//   program produces is the mistake; a member that generates what it compiles
//   cannot have it.
//
// The generators are deterministic — same inputs, same bytes — so three copies
// cannot disagree.
//
//   tools/gen-visibility-macros.py  versions-macros   → glib/gversionmacros.h
//   tools/gen-visibility-macros.py  visibility-macros → <lib>-visibility.h
//   meson configure_file            glibconfig.h.in   → glibconfig.h
//   meson configure_file                              → gmodule/gmoduleconf.h
//   gobject/glib-mkenums (Python)                     → glib-enumtypes.{h,c}
//   meson configure_file                              → config.h
//
// All of them are pure text transformations over inputs in the tree, so none
// needs an interpreter.
//
// ⚠️ EVERYTHING GOES INTO <manifest>/include, NOT out_dir(). glib's sources say
// `#include "config.h"`, and a build program's `mcpp::include_dir()` is
// appended AFTER every dependency's — the wlroots fork measured six config.h
// on one include path and read the wrong one. Manifest `include_dirs` come
// first, and `include/.gitkeep` is committed because mcpp builds the command
// line BEFORE running this program and silently drops an entry that does not
// exist yet.
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// ⚠️ NO `import mcpp;` HERE. An import declaration has to come before any
// other declaration in the translation unit, so each build.mcpp does
//
//   import mcpp;
//   #include "../common/generators.h"
//
// in that order, and this header only uses the names.

namespace fs = std::filesystem;

namespace {

// glib 2.82.5. The minor version drives the version and visibility macro
// generators, which emit one block per stable minor release.
constexpr int MAJOR = 2, MINOR = 82, MICRO = 5;

fs::path g_root, g_up, g_inc;

// ⚠️ Flush stdout before writing to stderr. build.mcpp's stdout IS the
// directive protocol and mcpp reads both streams through one pipe; an
// unflushed directive and an eager stderr line splice into each other and the
// result is reported as a malformed directive. Learned in the wlroots fork,
// where 39 actions was enough output to cross the buffer boundary.
template <class... A>
void note(const char *fmt, A... a)
{
    std::fflush(stdout);
    std::fprintf(stderr, fmt, a...);
    std::fflush(stderr);
}

std::string slurp(const fs::path &p)
{
    std::ifstream in(p);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

// ── tools/gen-visibility-macros.py, versions-macros ─────────────────────────
//
// Expands @GLIB_VERSIONS@ in gversionmacros.h.in into one GLIB_VERSION_2_n
// block per even minor from 2 up to and including the current one.
void gen_version_macros()
{
    const fs::path in = g_up / "glib" / "gversionmacros.h.in";
    std::string body = slurp(in);
    const std::string marker = "@GLIB_VERSIONS@";
    const std::size_t at = body.find(marker);
    if (at == std::string::npos) {
        note("gversionmacros.h.in no longer contains @GLIB_VERSIONS@\n");
        std::exit(1);
    }
    // The whole LINE holding the marker is replaced, as the generator does by
    // writing its own lines in place of that one.
    std::size_t ls = body.rfind('\n', at);
    ls = (ls == std::string::npos) ? 0 : ls + 1;
    std::size_t le = body.find('\n', at);
    le = (le == std::string::npos) ? body.size() : le + 1;

    std::ostringstream v;
    for (int m = 2; m <= MINOR + 1; m += 2) {
        // `Since:` is max(minor, 32) — the versioning macros themselves only
        // arrived in 2.32, so the older ones are documented as of that release.
        const int since = m > 32 ? m : 32;
        v << "/**\n"
             "* GLIB_VERSION_2_" << m << ":\n"
             "*\n"
             "* A macro that evaluates to the 2." << m << " version of GLib, in a format\n"
             "* that can be used by the C pre-processor.\n"
             "*\n"
             "* Since: 2." << since << "\n"
             "*/\n"
             "#define GLIB_VERSION_2_" << m << "       (G_ENCODE_VERSION (2, " << m << "))\n";
    }

    fs::create_directories(g_inc / "glib");
    std::ofstream o(g_inc / "glib" / "gversionmacros.h");
    o << body.substr(0, ls) << v.str() << body.substr(le);
    mcpp::rerun_if_changed(in.string().c_str());
}

// ── tools/gen-visibility-macros.py, visibility-macros ───────────────────────
//
// One header per library, differing only in the namespace token. Reproduced
// exactly: the availability macros are what every glib header uses to decorate
// its declarations, so a difference here is a difference in every symbol's
// visibility and deprecation behaviour.
void gen_visibility(const char *ns, const fs::path &out)
{
    fs::create_directories(out.parent_path());
    std::ofstream f(out);
    const std::string n = ns;
    f << "#pragma once\n"
         "\n/* GENERATED by build.mcpp, replacing tools/gen-visibility-macros.py. */\n\n"
      << "#if (defined(_WIN32) || defined(__CYGWIN__)) && !defined(" << n << "_STATIC_COMPILATION)\n"
         "#  define _" << n << "_EXPORT __declspec(dllexport)\n"
         "#  define _" << n << "_IMPORT __declspec(dllimport)\n"
         "#elif __GNUC__ >= 4\n"
         "#  define _" << n << "_EXPORT __attribute__((visibility(\"default\")))\n"
         "#  define _" << n << "_IMPORT\n"
         "#else\n"
         "#  define _" << n << "_EXPORT\n"
         "#  define _" << n << "_IMPORT\n"
         "#endif\n"
         "#ifdef " << n << "_COMPILATION\n"
         "#  define _" << n << "_API _" << n << "_EXPORT\n"
         "#else\n"
         "#  define _" << n << "_API _" << n << "_IMPORT\n"
         "#endif\n\n"
         "#define _" << n << "_EXTERN _" << n << "_API extern\n\n"
         "#define " << n << "_VAR _" << n << "_EXTERN\n"
         "#define " << n << "_AVAILABLE_IN_ALL _" << n << "_EXTERN\n\n"
         "#ifdef GLIB_DISABLE_DEPRECATION_WARNINGS\n"
         "#define " << n << "_DEPRECATED _" << n << "_EXTERN\n"
         "#define " << n << "_DEPRECATED_FOR(f) _" << n << "_EXTERN\n"
         "#define " << n << "_UNAVAILABLE(maj,min) _" << n << "_EXTERN\n"
         "#define " << n << "_UNAVAILABLE_STATIC_INLINE(maj,min)\n"
         "#else\n"
         "#define " << n << "_DEPRECATED G_DEPRECATED _" << n << "_EXTERN\n"
         "#define " << n << "_DEPRECATED_FOR(f) G_DEPRECATED_FOR(f) _" << n << "_EXTERN\n"
         "#define " << n << "_UNAVAILABLE(maj,min) G_UNAVAILABLE(maj,min) _" << n << "_EXTERN\n"
         "#define " << n << "_UNAVAILABLE_STATIC_INLINE(maj,min) G_UNAVAILABLE(maj,min)\n"
         "#endif\n";

    // The generator starts at 26, not 2: nothing older than 2.26 is decorated.
    for (int m = 26; m <= MINOR + 1; m += 2) {
        const std::string M = std::to_string(m);
        f << "\n"
             "#if GLIB_VERSION_MIN_REQUIRED >= GLIB_VERSION_2_" << M << "\n"
             "#define " << n << "_DEPRECATED_IN_2_" << M << " " << n << "_DEPRECATED\n"
             "#define " << n << "_DEPRECATED_IN_2_" << M << "_FOR(f) " << n << "_DEPRECATED_FOR (f)\n"
             "#define " << n << "_DEPRECATED_MACRO_IN_2_" << M << " GLIB_DEPRECATED_MACRO\n"
             "#define " << n << "_DEPRECATED_MACRO_IN_2_" << M << "_FOR(f) GLIB_DEPRECATED_MACRO_FOR (f)\n"
             "#define " << n << "_DEPRECATED_ENUMERATOR_IN_2_" << M << " GLIB_DEPRECATED_ENUMERATOR\n"
             "#define " << n << "_DEPRECATED_ENUMERATOR_IN_2_" << M << "_FOR(f) GLIB_DEPRECATED_ENUMERATOR_FOR (f)\n"
             "#define " << n << "_DEPRECATED_TYPE_IN_2_" << M << " GLIB_DEPRECATED_TYPE\n"
             "#define " << n << "_DEPRECATED_TYPE_IN_2_" << M << "_FOR(f) GLIB_DEPRECATED_TYPE_FOR (f)\n"
             "#else\n"
             "#define " << n << "_DEPRECATED_IN_2_" << M << " _" << n << "_EXTERN\n"
             "#define " << n << "_DEPRECATED_IN_2_" << M << "_FOR(f) _" << n << "_EXTERN\n"
             "#define " << n << "_DEPRECATED_MACRO_IN_2_" << M << "\n"
             "#define " << n << "_DEPRECATED_MACRO_IN_2_" << M << "_FOR(f)\n"
             "#define " << n << "_DEPRECATED_ENUMERATOR_IN_2_" << M << "\n"
             "#define " << n << "_DEPRECATED_ENUMERATOR_IN_2_" << M << "_FOR(f)\n"
             "#define " << n << "_DEPRECATED_TYPE_IN_2_" << M << "\n"
             "#define " << n << "_DEPRECATED_TYPE_IN_2_" << M << "_FOR(f)\n"
             "#endif\n"
             "\n"
             "#if GLIB_VERSION_MAX_ALLOWED < GLIB_VERSION_2_" << M << "\n"
             "#define " << n << "_AVAILABLE_IN_2_" << M << " " << n << "_UNAVAILABLE (2, " << M << ")\n"
             "#define " << n << "_AVAILABLE_STATIC_INLINE_IN_2_" << M << " GLIB_UNAVAILABLE_STATIC_INLINE (2, " << M << ")\n"
             "#define " << n << "_AVAILABLE_MACRO_IN_2_" << M << " GLIB_UNAVAILABLE_MACRO (2, " << M << ")\n"
             "#define " << n << "_AVAILABLE_ENUMERATOR_IN_2_" << M << " GLIB_UNAVAILABLE_ENUMERATOR (2, " << M << ")\n"
             "#define " << n << "_AVAILABLE_TYPE_IN_2_" << M << " GLIB_UNAVAILABLE_TYPE (2, " << M << ")\n"
             "#else\n"
             "#define " << n << "_AVAILABLE_IN_2_" << M << " _" << n << "_EXTERN\n"
             "#define " << n << "_AVAILABLE_STATIC_INLINE_IN_2_" << M << "\n"
             "#define " << n << "_AVAILABLE_MACRO_IN_2_" << M << "\n"
             "#define " << n << "_AVAILABLE_ENUMERATOR_IN_2_" << M << "\n"
             "#define " << n << "_AVAILABLE_TYPE_IN_2_" << M << "\n"
             "#endif\n";
    }
}

// ── meson configure_file → glibconfig.h ─────────────────────────────────────
//
// The PUBLIC header every glib consumer includes: it fixes gint32, gsize,
// GPollFD, the byte order and the thread implementation. Sixty-six
// substitutions in glibconfig.h.in, and every one is a property of the target
// rather than a choice — which is why they can be written out rather than
// probed.
//
// ⚠️ THE POINTER-WIDTH ENTRIES ARE NOT CONSTANT. gsize, gssize, gintptr and
// the printf modifiers differ between LP64 and ILP32, so they come from
// target_arch() rather than from a copy of some machine's answers.
void gen_glibconfig()
{
    const std::string arch = mcpp::target_arch();
    const bool lp64 = !(arch == "i386" || arch == "i686" || arch == "arm"
                        || arch == "armv7" || arch == "x86" || arch == "riscv32");

    // On LP64, size_t is `unsigned long` and the printf modifier is "l"; on
    // ILP32 it is `unsigned int` with no modifier. glib exposes both through
    // G_GSIZE_FORMAT, which application code uses in printf.
    const char *size_type   = lp64 ? "\"unsigned long\"" : "\"unsigned int\"";
    const char *size_t_type = lp64 ? "unsigned long"     : "unsigned int";
    const char *ssize_type  = lp64 ? "long"              : "int";
    const char *size_mod    = lp64 ? "l"                 : "";
    const char *sizebits    = lp64 ? "64"                : "32";
    const char *intptr_def  = lp64 ? "typedef signed long gintptr;\ntypedef unsigned long guintptr;"
                                   : "typedef signed int gintptr;\ntypedef unsigned int guintptr;";
    const char *intptr_mod  = lp64 ? "\"l\"" : "\"\"";
    const char *intptr_fmt  = lp64 ? "\"li\"" : "\"i\"";
    const char *uintptr_fmt = lp64 ? "\"lu\"" : "\"u\"";
    const char *gint64      = lp64 ? "long"  : "long long";
    const char *gint64_mod  = lp64 ? "\"l\"" : "\"ll\"";
    const char *gint64_fmt  = lp64 ? "\"li\"" : "\"lli\"";
    const char *guint64_fmt = lp64 ? "\"lu\"" : "\"llu\"";
    const char *gint64_c    = lp64 ? "val##L"  : "val##LL";
    const char *guint64_c   = lp64 ? "val##UL" : "val##ULL";
    const char *glib_long   = lp64 ? "long" : "long";
    const char *longbits    = lp64 ? "64" : "32";

    fs::create_directories(g_inc);
    std::ofstream o(g_inc / "glibconfig.h");
    o << "/* GENERATED by build.mcpp from upstream/glib/glibconfig.h.in.\n"
         " *\n"
         " * The public configuration header. Every value here is a property of the\n"
         " * TARGET — type widths, byte order, the poll and socket constants — so it\n"
         " * is written rather than probed. The pointer-width entries follow\n"
         " * target_arch(): this build is " << (lp64 ? "LP64" : "ILP32") << " for " << arch << ". */\n"
         "#ifndef __G_LIBCONFIG_H__\n"
         "#define __G_LIBCONFIG_H__\n\n"
         "#include <glib/gmacros.h>\n\n"
         "#include <limits.h>\n"
         "#include <float.h>\n"
         "#define GLIB_HAVE_ALLOCA_H\n\n"
         "G_BEGIN_DECLS\n\n"
         "#define G_MINFLOAT\tFLT_MIN\n"
         "#define G_MAXFLOAT\tFLT_MAX\n"
         "#define G_MINDOUBLE\tDBL_MIN\n"
         "#define G_MAXDOUBLE\tDBL_MAX\n"
         "#define G_MINSHORT\tSHRT_MIN\n"
         "#define G_MAXSHORT\tSHRT_MAX\n"
         "#define G_MAXUSHORT\tUSHRT_MAX\n"
         "#define G_MININT\tINT_MIN\n"
         "#define G_MAXINT\tINT_MAX\n"
         "#define G_MAXUINT\tUINT_MAX\n"
         "#define G_MINLONG\tLONG_MIN\n"
         "#define G_MAXLONG\tLONG_MAX\n"
         "#define G_MAXULONG\tULONG_MAX\n\n"
         "typedef signed char gint8;\n"
         "typedef unsigned char guint8;\n"
         "typedef signed short gint16;\n"
         "typedef unsigned short guint16;\n"
         "#define G_GINT16_MODIFIER \"h\"\n"
         "#define G_GINT16_FORMAT \"hi\"\n"
         "#define G_GUINT16_FORMAT \"hu\"\n"
         "typedef signed int gint32;\n"
         "typedef unsigned int guint32;\n"
         "#define G_GINT32_MODIFIER \"\"\n"
         "#define G_GINT32_FORMAT \"i\"\n"
         "#define G_GUINT32_FORMAT \"u\"\n"
         "#define G_HAVE_GINT64 1\n\n"
         "typedef signed " << gint64 << " gint64;\n"
         "typedef unsigned " << gint64 << " guint64;\n\n"
         "#define G_GINT64_CONSTANT(val)\t(" << gint64_c << ")\n"
         "#define G_GUINT64_CONSTANT(val)\t(" << guint64_c << ")\n"
         "#define G_GINT64_MODIFIER " << gint64_mod << "\n"
         "#define G_GINT64_FORMAT " << gint64_fmt << "\n"
         "#define G_GUINT64_FORMAT " << guint64_fmt << "\n\n"
         "#define GLIB_SIZEOF_VOID_P " << (lp64 ? 8 : 4) << "\n"
         "#define GLIB_SIZEOF_LONG " << (lp64 ? 8 : 4) << "\n"
         "#define GLIB_SIZEOF_SIZE_T " << (lp64 ? 8 : 4) << "\n"
         "#define GLIB_SIZEOF_SSIZE_T " << (lp64 ? 8 : 4) << "\n\n"
         "typedef " << size_t_type << " gsize;\n"
         "typedef " << ssize_type << " gssize;\n"
         "#define G_GSIZE_MODIFIER \"" << size_mod << "\"\n"
         "#define G_GSSIZE_MODIFIER \"" << size_mod << "\"\n"
         "#define G_GSIZE_FORMAT \"" << size_mod << "u\"\n"
         "#define G_GSSIZE_FORMAT \"" << size_mod << "i\"\n\n"
         "#define G_MAXSIZE\tG_MAXU" << (lp64 ? "LONG" : "INT") << "\n"
         "#define G_MINSSIZE\tG_MIN" << (lp64 ? "LONG" : "INT") << "\n"
         "#define G_MAXSSIZE\tG_MAX" << (lp64 ? "LONG" : "INT") << "\n\n"
         "typedef gint64 goffset;\n"
         "#define G_MINOFFSET\tG_MININT64\n"
         "#define G_MAXOFFSET\tG_MAXINT64\n\n"
         "#define G_GOFFSET_MODIFIER      " << gint64_mod << "\n"
         "#define G_GOFFSET_FORMAT        " << gint64_fmt << "\n"
         "#define G_GOFFSET_CONSTANT(val) G_GINT64_CONSTANT(val)\n\n"
         "#define G_POLLFD_FORMAT \"%d\"\n\n"
         "#define GPOINTER_TO_INT(p)\t((gint)  (glong) (p))\n"
         "#define GPOINTER_TO_UINT(p)\t((guint) (gulong) (p))\n\n"
         "#define GINT_TO_POINTER(i)\t((gpointer) (glong) (i))\n"
         "#define GUINT_TO_POINTER(u)\t((gpointer) (gulong) (u))\n\n"
      << intptr_def << "\n"
         "#define G_GINTPTR_MODIFIER      " << intptr_mod << "\n"
         "#define G_GINTPTR_FORMAT        " << intptr_fmt << "\n"
         "#define G_GUINTPTR_FORMAT       " << uintptr_fmt << "\n\n"
         "#define GLIB_MAJOR_VERSION " << MAJOR << "\n"
         "#define GLIB_MINOR_VERSION " << MINOR << "\n"
         "#define GLIB_MICRO_VERSION " << MICRO << "\n\n"
         "#define G_OS_UNIX\n\n"
         "#define G_VA_COPY va_copy\n"
         "#define G_VA_COPY_AS_ARRAY 1\n\n"
         "#ifndef __cplusplus\n"
         "# define G_HAVE_ISO_VARARGS 1\n"
         "#endif\n"
         "#ifdef __cplusplus\n"
         "# define G_HAVE_ISO_VARARGS 1\n"
         "#endif\n\n"
         "#define G_HAVE_GROWING_STACK 0\n"
         "#define G_HAVE_GNUC_VISIBILITY 1\n\n"
         "/* ⚠️ G_GNUC_INTERNAL LIVES HERE, not in gmacros.h. Omitting it is not\n"
         " * a missing convenience: gmodule-deprecated.c declares\n"
         " * `G_GNUC_INTERNAL gchar* _g_module_build_path(...)` and the failure\n"
         " * is `unknown type name G_GNUC_INTERNAL`, reported against a source\n"
         " * file rather than against the header that should have defined it. */\n"
         "#if defined(__GNUC__) \&\& (__GNUC__ >= 4)\n"
         "#define G_GNUC_INTERNAL __attribute__((visibility(\"hidden\")))\n"
         "#elif defined(__SUNPRO_C) \&\& (__SUNPRO_C >= 0x550)\n"
         "#define G_GNUC_INTERNAL __hidden\n"
         "#elif defined (__GNUC__) \&\& defined (G_HAVE_GNUC_VISIBILITY)\n"
         "#define G_GNUC_INTERNAL __attribute__((visibility(\"hidden\")))\n"
         "#else\n"
         "#define G_GNUC_INTERNAL\n"
         "#endif\n\n"
         "/* Read by gprintf.c to decide whether to use the system printf or\n"
         " * glib's bundled gnulib one. glibc's is C99-conformant, which is what\n"
         " * HAVE_GOOD_PRINTF in config.h asserts, so the system one it is —\n"
         " * and none of glib/gnulib is compiled. */\n"
         "#define GLIB_USING_SYSTEM_PRINTF\n\n"
         "#ifndef _MSC_VER\n"
         "# define G_HAVE_GNUC_VARARGS 1\n"
         "#endif\n\n"
         "#define G_THREADS_ENABLED\n"
         "#define G_THREADS_IMPL_POSIX\n\n"
         "#define G_ATOMIC_LOCK_FREE\n\n"
         "#define GINT16_TO_LE(val)\t((gint16) (val))\n"
         "#define GUINT16_TO_LE(val)\t((guint16) (val))\n"
         "#define GINT16_TO_BE(val)\t((gint16) GUINT16_SWAP_LE_BE (val))\n"
         "#define GUINT16_TO_BE(val)\t(GUINT16_SWAP_LE_BE (val))\n"
         "#define GINT32_TO_LE(val)\t((gint32) (val))\n"
         "#define GUINT32_TO_LE(val)\t((guint32) (val))\n"
         "#define GINT32_TO_BE(val)\t((gint32) GUINT32_SWAP_LE_BE (val))\n"
         "#define GUINT32_TO_BE(val)\t(GUINT32_SWAP_LE_BE (val))\n"
         "#define GINT64_TO_LE(val)\t((gint64) (val))\n"
         "#define GUINT64_TO_LE(val)\t((guint64) (val))\n"
         "#define GINT64_TO_BE(val)\t((gint64) GUINT64_SWAP_LE_BE (val))\n"
         "#define GUINT64_TO_BE(val)\t(GUINT64_SWAP_LE_BE (val))\n"
         "#define GLONG_TO_LE(val)\t((glong) GINT" << longbits << "_TO_LE (val))\n"
         "#define GULONG_TO_LE(val)\t((gulong) GUINT" << longbits << "_TO_LE (val))\n"
         "#define GLONG_TO_BE(val)\t((glong) GINT" << longbits << "_TO_BE (val))\n"
         "#define GULONG_TO_BE(val)\t((gulong) GUINT" << longbits << "_TO_BE (val))\n"
         "#define GINT_TO_LE(val)\t\t((gint) GINT32_TO_LE (val))\n"
         "#define GUINT_TO_LE(val)\t((guint) GUINT32_TO_LE (val))\n"
         "#define GINT_TO_BE(val)\t\t((gint) GINT32_TO_BE (val))\n"
         "#define GUINT_TO_BE(val)\t((guint) GUINT32_TO_BE (val))\n"
         "#define GSIZE_TO_LE(val)\t((gsize) GUINT" << sizebits << "_TO_LE (val))\n"
         "#define GSSIZE_TO_LE(val)\t((gssize) GINT" << sizebits << "_TO_LE (val))\n"
         "#define GSIZE_TO_BE(val)\t((gsize) GUINT" << sizebits << "_TO_BE (val))\n"
         "#define GSSIZE_TO_BE(val)\t((gssize) GINT" << sizebits << "_TO_BE (val))\n"
         "#define G_BYTE_ORDER G_LITTLE_ENDIAN\n\n"
         "#define GLIB_SYSDEF_POLLIN =1\n"
         "#define GLIB_SYSDEF_POLLOUT =4\n"
         "#define GLIB_SYSDEF_POLLPRI =2\n"
         "#define GLIB_SYSDEF_POLLHUP =16\n"
         "#define GLIB_SYSDEF_POLLERR =8\n"
         "#define GLIB_SYSDEF_POLLNVAL =32\n\n"
         "#define G_MODULE_SUFFIX \"so\"\n\n"
         "typedef int GPid;\n"
         "#define G_PID_FORMAT \"i\"\n\n"
         "#define GLIB_SYSDEF_AF_UNIX 1\n"
         "#define GLIB_SYSDEF_AF_INET 2\n"
         "#define GLIB_SYSDEF_AF_INET6 10\n\n"
         "#define GLIB_SYSDEF_MSG_OOB 1\n"
         "#define GLIB_SYSDEF_MSG_PEEK 2\n"
         "#define GLIB_SYSDEF_MSG_DONTROUTE 4\n\n"
         "#define G_DIR_SEPARATOR '/'\n"
         "#define G_DIR_SEPARATOR_S \"/\"\n"
         "#define G_SEARCHPATH_SEPARATOR ':'\n"
         "#define G_SEARCHPATH_SEPARATOR_S \":\"\n\n"
         "G_END_DECLS\n\n"
         "#undef GLIB_STATIC_COMPILATION\n"
         "#undef GOBJECT_STATIC_COMPILATION\n"
         "#undef GIO_STATIC_COMPILATION\n"
         "#undef GMODULE_STATIC_COMPILATION\n"
         "#undef GI_STATIC_COMPILATION\n"
         "#undef G_INTL_STATIC_COMPILATION\n"
         "#undef FFI_STATIC_BUILD\n\n"
         "#endif /* __G_LIBCONFIG_H__ */\n";
    (void)size_type;
    (void)glib_long;
    note("glibconfig.h: %s for %s\n", lp64 ? "LP64" : "ILP32", arch.c_str());
}

// ── meson configure_file → gmodule/gmoduleconf.h ────────────────────────────
//
// Four substitutions, all decided by the platform. On Linux the loader is
// dlopen, `dlerror` exists, symbols carry no leading underscore and
// RTLD_GLOBAL works — so three of the four are 0 and the interesting one is
// G_MODULE_IMPL.
//
// ⚠️ The template tests three of them with `#if (@X@)` — a VALUE, not a
// definition — so 0 is the right way to say no here, and leaving them out
// would be a syntax error rather than a wrong answer. The opposite of the
// `#ifdef` rule that governs config.h two functions down; the test style
// decides, every time.
void gen_gmoduleconf()
{
    const fs::path in = g_up / "gmodule" / "gmoduleconf.h.in";
    std::string body = slurp(in);
    struct { const char *k, *v; } vals[] = {
        {"@G_MODULE_IMPL@",                "G_MODULE_IMPL_DL"},
        {"@G_MODULE_HAVE_DLERROR@",        "1"},
        {"@G_MODULE_NEED_USCORE@",         "0"},
        {"@G_MODULE_BROKEN_RTLD_GLOBAL@",  "0"},
    };
    for (const auto &s : vals)
        for (std::size_t p = body.find(s.k); p != std::string::npos; p = body.find(s.k, p))
            body.replace(p, std::string(s.k).size(), s.v);

    // ⚠️ FLAT, not under `gmodule/`. gmodule.c includes it by bare name, and an
    // `include_dirs` entry naming a subdirectory that does not exist YET is
    // SILENTLY DROPPED — mcpp builds the compiler command line before running
    // this program, and only `include/` itself is committed (as .gitkeep).
    // Measured: a fresh extraction failed with `gmoduleconf.h: No such file or
    // directory` while every warm tree passed.
    std::ofstream(g_inc / "gmoduleconf.h")
        << "/* GENERATED by build.mcpp from upstream/gmodule/gmoduleconf.h.in. */\n"
        << body;
    mcpp::rerun_if_changed(in.string().c_str());
}

// ── gobject/glib-mkenums, for the one header it is pointed at ───────────────
//
// Upstream's glib-mkenums is 816 lines of Python that scans headers for enum
// declarations, honours `/*< … >*/` trigraph annotations, and expands a
// template. This build needs it for exactly ONE input — `glib/gunicode.h`,
// which contains four enums — so what is reproduced here is the subset that
// covers those four, not the whole tool.
//
// The tool is CALLED TWICE with different templates, and the templates are read
// from the tree rather than hard-coded, so a change upstream makes to them is
// picked up.
struct enum_def {
    std::string name;          // GUnicodeType            — as written
    std::string prefix;        // G_UNICODE_             — from the ENUMERATORS, for nicks
    std::string macro_prefix;  // G                      — from the TYPE NAME, for @ENUMPREFIX@
    std::string shortname;     // UNICODE_TYPE           — @ENUMSHORT@
    std::string long_name;     // G_UNICODE_TYPE         — @ENUMNAME@
    std::string sym_name;      // g_unicode_type         — @enum_name@
    std::vector<std::string> values;
    // Per-enumerator nick, when `/*< nick=… >*/` overrides the derived one.
    // Empty means "derive it": prefix stripped, lower-cased, underscores to
    // hyphens. gio uses the override 17 times, including `nick=none` for the
    // zero value of a flags type, where the derived nick would be the whole
    // name.
    std::vector<std::string> nicks;
    bool flags = false;
};

// ⚠️ `/*< private >*/`, `/*< public >*/` and `/*< protected >*/` are GTK-DOC
// annotations for STRUCT MEMBERS, not mkenums annotations — and gio has 144 of
// them against 25 real ones. A scanner that treated every `/*< … >*/` as
// meaningful would read them, and `private` contains no `=`, so the damage
// would be quiet: an enumerator silently skipped rather than an error.
bool is_mkenums_annotation(const std::string &body)
{
    for (const char *a : {"flags", "skip", "nick", "underscore_name", "prefix", "since"})
        if (body.find(a) != std::string::npos) return true;
    return false;
}

// The text inside `/*< … >*/` on a line, or empty.
std::string annotation_of(const std::string &line)
{
    const std::size_t a = line.find("/*<");
    if (a == std::string::npos) return {};
    const std::size_t b = line.find(">*/", a);
    if (b == std::string::npos) return {};
    return line.substr(a + 3, b - a - 3);
}

// `nick=supports-uris` → `supports-uris`. Empty if the key is absent.
std::string annotation_value(const std::string &ann, const std::string &key)
{
    const std::size_t k = ann.find(key + "=");
    if (k == std::string::npos) return {};
    std::size_t s = k + key.size() + 1;
    std::size_t e = s;
    while (e < ann.size() && !std::isspace((unsigned char)ann[e]) && ann[e] != ',') ++e;
    return ann.substr(s, e - s);
}

// `GUnicodeBreakType` → prefix `G_UNICODE_BREAK`, short `TYPE`.
// mkenums derives these by splitting the CamelCase name into words, taking the
// longest prefix shared with the enumerators' common prefix. The four enums in
// gunicode.h all follow the plain rule, so the split is computed from the
// values themselves — which is what mkenums does and is why it needs no table.
// ⚠️ TWO DIFFERENT PREFIXES, and conflating them is how this generator shipped
// four wrong macro names in gnome.gobject 2.82.5.
//
//   enum_prefix       from the ENUMERATORS.  Drives the nicks.
//   enumname_prefix   from the TYPE NAME.    Drives @ENUMPREFIX@.
//
// glib-mkenums computes the second as the leading namespace segment of the
// type name — `^([A-Z][a-z]*)` of `GTlsChannelBindingType` is just `G` — so
// `@ENUMPREFIX@_TYPE_@ENUMSHORT@` renders `G_TYPE_TLS_CHANNEL_BINDING_TYPE`,
// the familiar GObject spelling.
//
// Deriving it from the enumerators instead (which is what this did) gives
// `G_TLS_CHANNEL_BINDING_TLS` and therefore
// `G_TLS_CHANNEL_BINDING_TLS_TYPE_...`. Every function name stays right, so it
// compiles, links, and passes any test that calls `g_..._get_type()` — the
// macro a consumer actually writes is the only thing that is wrong. Measured
// against a distribution's own glib-enumtypes.h:
//
//   generated here            upstream
//   G_UNICODE_TYPE_TYPE       G_TYPE_UNICODE_TYPE
//   G_NORMALIZE_TYPE_MODE     G_TYPE_NORMALIZE_MODE
//
// The rule that follows: when reproducing a generator, READ ITS ALGORITHM.
// A rule inferred from its output on four inputs fit all four and was still
// wrong.
void derive_names(enum_def &e)
{
    if (e.values.empty() || e.name.empty()) return;

    // ── the nick prefix, from the enumerators ────────────────────────────
    // Longest common prefix of the value names, then trimmed so it ends in an
    // underscore: mkenums' `re.sub(r'_[^_]*$', '_', prefix)`.
    //
    // An explicit `/*< prefix=… >*/` overrides it, upper-cased with hyphens
    // turned to underscores and a trailing underscore ensured.
    if (e.prefix.empty()) {
        std::string lcp = e.values[0];
        for (const auto &v : e.values) {
            std::size_t i = 0;
            while (i < lcp.size() && i < v.size() && lcp[i] == v[i]) ++i;
            lcp.resize(i);
        }
        const std::size_t cut = lcp.rfind('_');
        e.prefix = (cut == std::string::npos) ? std::string() : lcp.substr(0, cut + 1);
    } else {
        for (auto &c : e.prefix)
            c = (c == '-') ? '_' : char(std::toupper((unsigned char)c));
        if (!e.prefix.empty() && e.prefix.back() != '_') e.prefix += '_';
    }

    // ── the macro prefix and short name, from the type name ──────────────
    // enspace = ^([A-Z][a-z]*)  →  "G" for GTlsChannelBindingType,
    //                              "G" for GIOStream (the [a-z]* matches none)
    std::size_t i = 0;
    std::string enspace;
    if (i < e.name.size() && std::isupper((unsigned char)e.name[i])) enspace += e.name[i++];
    while (i < e.name.size() && std::islower((unsigned char)e.name[i])) enspace += e.name[i++];

    std::string rest = e.name.substr(enspace.size());

    // ([^A-Z])([A-Z]) → \1_\2  :  "TlsChannelBindingType" → "Tls_Channel_Binding_Type"
    std::string s1;
    for (std::size_t k = 0; k < rest.size(); ++k) {
        if (k > 0 && std::isupper((unsigned char)rest[k])
            && !std::isupper((unsigned char)rest[k - 1]))
            s1 += '_';
        s1 += rest[k];
    }
    // ([A-Z][A-Z])([A-Z][0-9a-z]) → \1_\2  :  "IOStream" → "IO_Stream"
    std::string s2;
    for (std::size_t k = 0; k < s1.size(); ++k) {
        if (k >= 2 && k + 1 < s1.size()
            && std::isupper((unsigned char)s1[k - 2]) && std::isupper((unsigned char)s1[k - 1])
            && std::isupper((unsigned char)s1[k])
            && (std::islower((unsigned char)s1[k + 1]) || std::isdigit((unsigned char)s1[k + 1])))
            s2 += '_';
        s2 += s1[k];
    }
    for (auto &c : s2) c = char(std::toupper((unsigned char)c));

    e.shortname       = s2;
    e.macro_prefix    = enspace;
    for (auto &c : e.macro_prefix) c = char(std::toupper((unsigned char)c));
    e.long_name       = e.macro_prefix + "_" + e.shortname;
    e.sym_name        = e.long_name;
    for (auto &c : e.sym_name) c = char(std::tolower((unsigned char)c));
}

std::vector<enum_def> scan_enums(const fs::path &header)
{
    std::vector<enum_def> out;
    std::ifstream in(header);
    std::string line;
    bool inside = false;
    enum_def cur;
    while (std::getline(in, line)) {
        const std::string ann = annotation_of(line);
        if (!inside) {
            if (line.find("typedef enum") != std::string::npos) {
                inside = true;
                cur = enum_def{};
                // `/*< flags >*/` marks a flags type, which mkenums renders
                // with g_flags_register_static instead of g_enum_register_static.
                cur.flags = !ann.empty() && ann.find("flags") != std::string::npos;
                // `/*< prefix=G_APPLICATION >*/` overrides the derived prefix.
                // gio uses it once, on GApplicationFlags, where the enumerators
                // share G_APPLICATION_ but the type name would derive
                // G_APPLICATION_FLAGS.
                cur.prefix = annotation_value(ann, "prefix");
            }
            continue;
        }
        const std::size_t close = line.find('}');
        if (close != std::string::npos) {
            // ⚠️ The FIRST identifier after `}`, not everything up to the `;`.
            // gio writes `} GTlsRehandshakeMode GIO_DEPRECATED_TYPE_IN_2_60;`
            // — the deprecation decoration follows the name — and taking the
            // whole span produced a type called
            // `GTlsRehandshakeMode GIO_DEPRECATED_TYPE_IN_2_60`, which the
            // name manglers then turned into
            // `_g_i_o__d_e_p_r_e_c_a_t_e_d__t_y_p_e__i_n_2_60_get_type`.
            std::size_t s = line.find_first_not_of(" \t", close + 1);
            std::size_t e = s;
            while (e != std::string::npos && e < line.size()
                   && (std::isalnum((unsigned char)line[e]) || line[e] == '_')) ++e;
            if (s != std::string::npos && e > s) {
                cur.name = line.substr(s, e - s);
                derive_names(cur);
                if (!cur.name.empty() && !cur.values.empty()) out.push_back(cur);
            }
            inside = false;
            continue;
        }
        // An enumerator: the first identifier on the line, if the line is not a
        // comment, a preprocessor directive, or an annotation of its own.
        const std::size_t s = line.find_first_not_of(" \t");
        if (s == std::string::npos) continue;
        if (line[s] == '/' || line[s] == '*' || line[s] == '#') continue;
        // `/*< skip >*/` on an enumerator removes it from the registered type.
        // gio never uses it; glib's other headers do, and honouring it costs a
        // line.
        if (!ann.empty() && is_mkenums_annotation(ann)
            && ann.find("skip") != std::string::npos) continue;
        std::size_t e = s;
        while (e < line.size() && (std::isalnum((unsigned char)line[e]) || line[e] == '_')) ++e;
        if (e > s) {
            cur.values.push_back(line.substr(s, e - s));
            cur.nicks.push_back(is_mkenums_annotation(ann) ? annotation_value(ann, "nick")
                                                           : std::string());
        }
    }
    return out;
}

// Expand one of upstream's `.template` files. The format is a sequence of
// `/*** BEGIN <section> ***/ … /*** END <section> ***/` blocks; mkenums emits
// file-header once, then file-production and value-header per input file and
// per enum, then file-tail.
std::string section(const std::string &tpl, const std::string &name)
{
    const std::string b = "/*** BEGIN " + name + " ***/";
    const std::string e = "/*** END " + name + " ***/";
    const std::size_t s = tpl.find(b);
    if (s == std::string::npos) return {};
    const std::size_t bodyStart = tpl.find('\n', s);
    const std::size_t bodyEnd = tpl.find(e, bodyStart);
    if (bodyStart == std::string::npos || bodyEnd == std::string::npos) return {};
    return tpl.substr(bodyStart + 1, bodyEnd - bodyStart - 1);
}

std::string subst(std::string s, const std::string &k, const std::string &v)
{
    for (std::size_t p = s.find(k); p != std::string::npos; p = s.find(k, p + v.size()))
        s.replace(p, k.size(), v);
    return s;
}

// Expand one of upstream's `.template` files over a set of enums, grouped by
// the header each came from.
//
// Shared by glib's four enums and gio's eighty-one: the template format is the
// same (`file-header`, `file-production` per input file, `value-header` /
// `value-production` / `value-tail` per enum, `file-tail`), and the only thing
// that differs is how many there are and which directory the templates sit in.
void write_enumtypes(const fs::path &tplPath, const fs::path &out,
                     const std::vector<std::pair<std::string, std::vector<enum_def>>> &per_file)
{
    const std::string tpl = slurp(tplPath);
    mcpp::rerun_if_changed(tplPath.string().c_str());

    std::ostringstream o;
    o << "/* GENERATED by build.mcpp, replacing gobject/glib-mkenums. */\n";
    o << section(tpl, "file-header");
    for (const auto &[filename, enums] : per_file) {
        o << subst(section(tpl, "file-production"), "@filename@", filename);
        for (const auto &e : enums) {
            std::string vh = section(tpl, "value-header");
            vh = subst(vh, "@enum_name@", e.sym_name);
            vh = subst(vh, "@EnumName@", e.name);
            vh = subst(vh, "@ENUMNAME@", e.long_name);
            vh = subst(vh, "@ENUMPREFIX@", e.macro_prefix);
            vh = subst(vh, "@ENUMSHORT@", e.shortname);
            vh = subst(vh, "@type@", e.flags ? "flags" : "enum");
            vh = subst(vh, "@Type@", e.flags ? "Flags" : "Enum");

            std::string body;
            for (std::size_t vi = 0; vi < e.values.size(); ++vi) {
                const std::string &v = e.values[vi];
                std::string vp = section(tpl, "value-production");
                if (vp.empty()) break;
                vp = subst(vp, "@VALUENAME@", v);
                vp = subst(vp, "@valuenick@", [&] {
                    // ⚠️ An explicit `/*< nick=… >*/` WINS. The nick is public
                    // API — g_flags_get_value_by_nick reads it — and the
                    // derived guess is wrong where upstream annotated:
                    // G_CONVERTER_NO_FLAGS would derive "no-flags" and
                    // upstream says "none".
                    if (vi < e.nicks.size() && !e.nicks[vi].empty()) return e.nicks[vi];
                    std::string n = (v.size() > e.prefix.size()
                                     && v.compare(0, e.prefix.size(), e.prefix) == 0)
                                        ? v.substr(e.prefix.size()) : v;
                    for (auto &c : n) c = (c == '_') ? '-' : char(std::tolower((unsigned char)c));
                    return n;
                }());
                vp = subst(vp, "@valuenum@", v);
                body += vp;
            }

            std::string vt = section(tpl, "value-tail");
            vt = subst(vt, "@EnumName@", e.name);
            vt = subst(vt, "@enum_name@", e.sym_name);
            vt = subst(vt, "@ENUMNAME@", e.long_name);
            vt = subst(vt, "@ENUMPREFIX@", e.macro_prefix);
            vt = subst(vt, "@ENUMSHORT@", e.shortname);
            vt = subst(vt, "@type@", e.flags ? "flags" : "enum");
            vt = subst(vt, "@Type@", e.flags ? "Flags" : "Enum");
            o << vh << body << vt;
        }
    }
    o << section(tpl, "file-tail");

    std::error_code ec;
    fs::create_directories(out.parent_path(), ec);
    std::ofstream(out) << o.str();
}

// glib's four, from the one header mkenums is pointed at.
void gen_enumtypes()
{
    const fs::path header = g_up / "glib" / "gunicode.h";
    const auto enums = scan_enums(header);
    if (enums.size() != 4) {
        // The count is asserted because a silent drop would produce a header
        // that compiles and a library missing g_unicode_script_get_type — the
        // failure would land in a consumer, not here.
        note("gunicode.h yielded %zu enums, expected 4 "
             "(GUnicodeType, GUnicodeBreakType, GUnicodeScript, GNormalizeMode)\n",
             enums.size());
        std::exit(1);
    }
    mcpp::rerun_if_changed(header.string().c_str());

    const std::vector<std::pair<std::string, std::vector<enum_def>>> per_file{
        {"gunicode.h", enums}};
    write_enumtypes(g_up / "gobject" / "glib-enumtypes.h.template",
                    g_inc / "gobject" / "glib-enumtypes.h", per_file);
    write_enumtypes(g_up / "gobject" / "glib-enumtypes.c.template",
                    g_inc / "gobject" / "glib-enumtypes.c", per_file);
    note("glib-enumtypes: %zu enums from gunicode.h\n", enums.size());
}

// ── meson configure_file → config.h ─────────────────────────────────────────
//
// The internal probe answers. Everything here describes glibc on Linux with a
// GCC-compatible compiler, which is the only configuration this package builds
// for — and, as always, describes THIS build's dependencies rather than
// upstream's defaults.
void gen_config()
{
    std::ofstream o(g_inc / "config.h");
    o << "/* GENERATED by build.mcpp — meson's configure_file output for glib\n"
         " * on Linux/glibc with a GCC-compatible compiler. */\n"
         "#ifndef MCPP_GLIB_CONFIG_H\n"
         "#define MCPP_GLIB_CONFIG_H\n\n"
         "#define GLIB_VERSION \"" << MAJOR << "." << MINOR << "." << MICRO << "\"\n"
         "#define PACKAGE_VERSION \"" << MAJOR << "." << MINOR << "." << MICRO << "\"\n"
         "#define PACKAGE_NAME \"glib\"\n"
         "#define PACKAGE_STRING \"glib " << MAJOR << "." << MINOR << "." << MICRO << "\"\n"
         "#define PACKAGE_TARNAME \"glib\"\n"
         "#define PACKAGE_BUGREPORT \"https://gitlab.gnome.org/GNOME/glib/issues/new\"\n"
         "#define VERSION \"" << MAJOR << "." << MINOR << "." << MICRO << "\"\n\n"
         "/* ⚠️ ENABLE_NLS must be ON here. Without it glibintl.h:33 does\n"
         " * `#define gettext(String) (String)` — a function-like macro — and\n"
         " * ggettext.c then includes <libintl.h>, whose\n"
         " * `extern char *gettext (const char *)` gets macro-expanded into\n"
         " * nonsense: `expected identifier or '(' before 'const'`, reported\n"
         " * against glibc's header. glibc carries gettext in libc itself, so\n"
         " * there is nothing extra to link. */\n"
         "#define ENABLE_NLS 1\n"
         "#define HAVE_LIBINTL_H 1\n"
         "#define GETTEXT_PACKAGE \"glib20\"\n"
         "#define GLIB_LOCALE_DIR \"/usr/share/locale\"\n"
         "#define GLIB_LOCALSTATEDIR \"/var\"\n"
         "/* gio's gunixmounts.c reads /run/mount/utab for the mount options\n"
         " * that /proc/self/mountinfo does not carry. FHS 3.0 fixed the\n"
         " * location at /run. */\n"
         "#define GLIB_RUNSTATEDIR \"/run\"\n"
         "#define GLIB_BINARY_AGE " << (MINOR * 100 + MICRO) << "\n"
         "#define GLIB_INTERFACE_AGE " << MICRO << "\n\n"
         "/* ⚠️ EVERY PROBE BELOW IS EITHER DEFINED OR ABSENT. NONE IS 0.\n"
         " *\n"
         " * autotools projects test these with `#ifdef`, and `#define HAVE_X 0`\n"
         " * is therefore a statement that X EXISTS. Writing the negative answers\n"
         " * as 0 here — HAVE_ISSETUGID, HAVE_LCHMOD, HAVE_GETFSSTAT and six more\n"
         " * — made glib call BSD interfaces glibc does not have:\n"
         " *\n"
         " *   gutils.c:3291: implicit declaration of function 'issetugid'\n"
         " *\n"
         " * The same mistake cost cairo an hour of silently drawing nothing.\n"
         " * The absent ones are listed at the end of this file with their\n"
         " * consequences, because absence is otherwise indistinguishable from\n"
         " * an oversight. */\n\n"
         "/* ── headers ────────────────────────────────────────────────────── */\n"
         "#define HAVE_ALLOCA_H 1\n#define HAVE_DIRENT_H 1\n#define HAVE_DLFCN_H 1\n"
         "#define HAVE_SPAWN_H 1\n"
         "#define HAVE_FLOAT_H 1\n#define HAVE_GRP_H 1\n#define HAVE_INTTYPES_H 1\n"
         "#define HAVE_LIMITS_H 1\n#define HAVE_LINUX_MAGIC_H 1\n#define HAVE_LOCALE_H 1\n"
         "#define HAVE_MEMORY_H 1\n#define HAVE_MNTENT_H 1\n#define HAVE_PWD_H 1\n"
         "#define HAVE_SCHED_H 1\n#define HAVE_STDINT_H 1\n#define HAVE_STDLIB_H 1\n"
         "#define HAVE_STRINGS_H 1\n#define HAVE_STRING_H 1\n#define HAVE_SYS_INOTIFY_H 1\n"
         "#define HAVE_SYS_MOUNT_H 1\n#define HAVE_SYS_PARAM_H 1\n#define HAVE_SYS_RESOURCE_H 1\n"
         "#define HAVE_SYS_SELECT_H 1\n#define HAVE_SYS_STATFS_H 1\n#define HAVE_SYS_STATVFS_H 1\n"
         "#define HAVE_SYS_STAT_H 1\n#define HAVE_SYS_SYSMACROS_H 1\n#define HAVE_SYS_TIMES_H 1\n"
         "#define HAVE_SYS_TIME_H 1\n#define HAVE_SYS_TYPES_H 1\n#define HAVE_SYS_UIO_H 1\n"
         "#define HAVE_SYS_VFS_H 1\n#define HAVE_SYS_WAIT_H 1\n#define HAVE_SYS_XATTR_H 1\n"
         "#define HAVE_TERMIOS_H 1\n#define HAVE_UNISTD_H 1\n#define HAVE_VALUES_H 1\n"
         "#define HAVE_WCHAR_H 1\n#define HAVE_WCTYPE_H 1\n\n"
         "/* ── functions ──────────────────────────────────────────────────── */\n"
         "#define HAVE_ACCEPT4 1\n#define HAVE_ALIGNED_ALLOC 1\n#define HAVE_BIND_TEXTDOMAIN_CODESET 1\n"
         "#define HAVE_CLOSE_RANGE 1\n#define HAVE_COPY_FILE_RANGE 1\n#define HAVE_DUP3 1\n"
         "#define HAVE_ENDMNTENT 1\n#define HAVE_ENDSERVENT 1\n#define HAVE_EPOLL_CREATE1 1\n"
         "#define HAVE_EVENTFD 1\n#define HAVE_FALLOCATE 1\n#define HAVE_FCHMOD 1\n"
         "#define HAVE_FCHOWN 1\n#define HAVE_FDWALK 1\n#define HAVE_FSYNC 1\n"
         "#define HAVE_FTRUNCATE64 1\n#define HAVE_GETAUXVAL 1\n#define HAVE_GETC_UNLOCKED 1\n"
         "#define HAVE_GETGRGID_R 1\n#define HAVE_GETMNTENT_R 1\n"
         "#define HAVE_GETPWUID_R 1\n#define HAVE_GETRESUID 1\n"
         "#define HAVE_GMTIME_R 1\n#define HAVE_HASMNTOPT 1\n#define HAVE_IF_INDEXTONAME 1\n"
         "#define HAVE_IF_NAMETOINDEX 1\n#define HAVE_INOTIFY_INIT1 1\n"
         "#define HAVE_KILL 1\n#define HAVE_LCHOWN 1\n"
         "#define HAVE_LINK 1\n#define HAVE_LOCALTIME_R 1\n#define HAVE_LSTAT 1\n"
         "#define HAVE_MEMALIGN 1\n#define HAVE_MEMMEM 1\n#define HAVE_MKOSTEMP 1\n"
         "#define HAVE_MMAP 1\n#define HAVE_NEWLOCALE 1\n#define HAVE_OPEN_MEMSTREAM 1\n"
         "#define HAVE_PIPE2 1\n#define HAVE_POLL 1\n#define HAVE_POSIX_MEMALIGN 1\n"
         "#define HAVE_POSIX_SPAWN 1\n#define HAVE_PRLIMIT 1\n#define HAVE_PROC_SELF_CMDLINE 1\n"
         "#define HAVE_PTHREAD_ATTR_SETINHERITSCHED 1\n"
         "#define HAVE_PTHREAD_CONDATTR_SETCLOCK 1\n"
         "#define HAVE_PTHREAD_GETNAME_NP 1\n#define HAVE_PTHREAD_SETNAME_NP_WITH_TID 1\n"
         "#define HAVE_READLINK 1\n#define HAVE_RECVMMSG 1\n#define HAVE_RES_INIT 1\n"
         "#define HAVE_RES_NCLOSE 1\n#define HAVE_RES_NINIT 1\n#define HAVE_RES_NQUERY 1\n"
         "#define HAVE_SENDMMSG 1\n#define HAVE_SETENV 1\n#define HAVE_SETMNTENT 1\n"
         "#define HAVE_SIG_ATOMIC_T 1\n#define HAVE_SIOCGIFADDR 1\n#define HAVE_SNPRINTF 1\n"
         "#define HAVE_SPLICE 1\n#define HAVE_STATFS 1\n#define HAVE_STATVFS 1\n"
         "#define HAVE_STPCPY 1\n#define HAVE_STRCASECMP 1\n#define HAVE_STRERROR_R 1\n"
         "/* ⚠️ glibc has TWO strerror_r. With _GNU_SOURCE it is the GNU one,\n"
         " * returning char*; the XSI one returns int. glib chooses with\n"
         " * `#ifdef STRERROR_R_CHAR_P`, and choosing wrong is a compile error\n"
         " * rather than a silent one — gstrfuncs.c:1320, `assignment to int\n"
         " * from char *`. This package passes -D_GNU_SOURCE, so: char*. */\n"
         "#define STRERROR_R_CHAR_P 1\n"
         "#define HAVE_STRNCASECMP 1\n#define HAVE_STRNLEN 1\n#define HAVE_STRSIGNAL 1\n"
         "#define HAVE_STRTOD_L 1\n#define HAVE_STRTOLL_L 1\n#define HAVE_STRTOULL_L 1\n"
         "#define HAVE_SYMLINK 1\n#define HAVE_TIMEGM 1\n"
         "#define HAVE_UNSETENV 1\n#define HAVE_USELOCALE 1\n#define HAVE_UTIMES 1\n"
         "#define HAVE_VALLOC 1\n#define HAVE_VASPRINTF 1\n#define HAVE_VSNPRINTF 1\n"
         "#define HAVE_WCRTOMB 1\n#define HAVE_WCSLEN 1\n#define HAVE_WCSNLEN 1\n\n"
         "/* ── types and members ──────────────────────────────────────────── */\n"
         "#define HAVE_LONG_LONG 1\n#define HAVE_LONG_DOUBLE 1\n"
         "#define HAVE_STRUCT_DIRENT_D_TYPE 1\n"
         ""
         "#define HAVE_STRUCT_STAT_ST_ATIM_TV_NSEC 1\n"
         "#define HAVE_STRUCT_STAT_ST_BLKSIZE 1\n#define HAVE_STRUCT_STAT_ST_BLOCKS 1\n"
         "#define HAVE_STRUCT_STATFS_F_BAVAIL 1\n"
         ""
         "#define HAVE_STRUCT_TM_TM_GMTOFF 1\n\n"
         "/* ── sizes, on the target this build is for ─────────────────────── */\n"
         "#define SIZEOF_CHAR 1\n#define SIZEOF_INT 4\n#define SIZEOF_SHORT 2\n"
         "#define SIZEOF_LONG_LONG 8\n#define SIZEOF_WCHAR_T 4\n\n"
         "/* ── things glib asks about itself ──────────────────────────────── */\n"
         "/* ⚠️ USE_STATFS / USE_STATVFS are `#ifdef`-tested. Defining the unused\n"
         " * one to 0 would say YES, and gio would call the wrong one. */\n"
         "#define USE_STATFS 1\n\n"
         "#define ALIGNOF_GUINT32 4\n#define ALIGNOF_GUINT64 8\n#define ALIGNOF_UNSIGNED_LONG 8\n"
         "#define G_VA_COPY_AS_ARRAY 1\n"
         "#define G_HAVE_GROWING_STACK 0\n"
         "/* ⚠️ TWO DIFFERENT MACROS, and only this one selects anything.\n"
         " * `USE_SYSTEM_PRINTF` lives in config.h and is what gprintfint.h\n"
         " * tests; `GLIB_USING_SYSTEM_PRINTF` lives in the PUBLIC glibconfig.h\n"
         " * and only reports the decision. Setting the public one alone leaves\n"
         " * glib calling its bundled gnulib printf, and since none of\n"
         " * glib/gnulib is compiled here the link fails with a page of\n"
         " * `undefined reference to _g_gnulib_snprintf`. */\n"
         "#define USE_SYSTEM_PRINTF 1\n"
         "#define HAVE_C99_SNPRINTF 1\n#define HAVE_C99_VSNPRINTF 1\n"
         "#define HAVE_UNIX98_PRINTF 1\n"
         "#define HAVE_GOOD_PRINTF 1\n\n"
         "/* THREADS_POSIX is `#ifdef`-tested throughout gthread; the Windows\n"
         " * alternative is THREADS_WIN32 and it stays absent. */\n"
         "#define THREADS_POSIX 1\n\n"
         "/* pcre2 comes from compat.pcre2, built without the JIT — glib checks\n"
         " * this at run time through pcre2_config() and takes the interpreter\n"
         " * path, so nothing here needs to say so. */\n"
         "\n"
         "/* No libmount, no sysprof, no selinux, no libelf: none is in this index\n"
         " * and each is `#ifdef`-tested, so each is ABSENT rather than 0.\n"
         " *   HAVE_LIBMOUNT     — gio's GUnixMountMonitor loses mount-option\n"
         " *                       detail and falls back to /proc/self/mountinfo\n"
         " *   HAVE_SYSPROF      — tracing\n"
         " *   HAVE_SELINUX      — gio's SELinux attribute support\n"
         " *   HAVE_LIBELF       — gio module scanning\n"
         " *   HAVE_XATTR        — needs libattr; gio degrades cleanly */\n\n"
         "#endif\n";
}

} // namespace

