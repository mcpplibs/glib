#pragma once
// The C++23 module wrappers, one per member.
//
// ─────────────────────────────────────────────────────────────────────────
// WHY THIS EXISTS: THE NAMESPACE IS A CONTRACT
//
// In this index the namespace tells a consumer HOW to consume the package:
//
//     compat.xxx      headers — `#include <foo.h>`, no module
//     <owner>.xxx     the package exposes `import`
//
// Every other owner-namespace package keeps that bargain — `freedesktop.cairo`
// exports 544 names, `wlroots.wlroots` 986, `freedesktop.libdisplay-info` 206.
// `gnome.*` did not, and a name that promises `import` and has none is a wrong
// promise rather than a missing nicety.
//
// ─────────────────────────────────────────────────────────────────────────
// ⚠️ THE MODULE CARRIES DECLARATIONS. MACROS STAY WITH THE HEADER ROUTE.
//
// A module cannot export macros. For wlroots that costs almost nothing — the
// only macros are the `WLR_HAS_*` booleans, and its test pairs `import
// wlroots;` with `#include <wlr/config.h>`. For glib it is HALF THE API:
//
//     glib      1,312 declarations   1,337 #define   (693 function-like)
//     gobject     243                  339           (229)
//     gio       1,753                1,679           (893)
//
// `G_DEFINE_TYPE`, `G_OBJECT`, `g_signal_connect`, every `G_TYPE_*` — macros.
//
// ⭐ AND THE TWO ROUTES DO NOT COMPOSE, WHICH IS WHAT SETTLES THE DESIGN.
// A TU that imports the module AND textually includes a glib header reaches
// <time.h> twice — once through the module's global fragment, once directly —
// and the same `struct tm` from the same file becomes two entities:
//
//     error: conflicting declaration 'struct tm'
//     note: previous declaration as 'struct tm'   (of module gnome.glib)
//
// So a consumer picks ONE route, and each member ships a test for each.
//
// ❌ TWO SHAPES WERE TRIED AND ABANDONED, both measured rather than reasoned:
//
//   a macro-only side header    Extracting all 1,928 `#define`s into a header
//                               that includes nothing works in the small — the
//                               probe compiled, linked and ran. It does not
//                               scale: glib's version and deprecation
//                               machinery is preprocessor-STATEFUL and ordered
//                               by the INCLUDE GRAPH, so a flat projection
//                               gave, in turn, both branches of an `#ifdef`,
//                               `#error "GLIB_VERSION_MIN_REQUIRED must be <=
//                               GLIB_VERSION_CUR_STABLE"`, and `missing binary
//                               operator before token 'GLIB_DEPRECATED_MACRO'`.
//                               Each fix uncovered the next, because the
//                               flattening is what is wrong. A faithful
//                               projection would be a partial preprocessor.
//
//   `export import <header>;`   A named module re-exporting a HEADER UNIT does
//                               carry every declaration — measured — but its
//                               importers do NOT get the macros. So the export
//                               list has to be enumerated either way, which is
//                               what the code below does.
//
// ─────────────────────────────────────────────────────────────────────────
// GENERATED, NOT WRITTEN. cairo's wrapper says why in one line: "a version
// bump regenerates it, so a name upstream added or removed cannot be silently
// missed." At 3,300 names that is not a preference.
//
// ❌ "ENUMERATORS COME FREE" WAS WRONG, AND IT WAS WRONG IN A WAY ONE
// TOOLCHAIN CANNOT SHOW YOU. Exporting only the typedef of an unnamed enum
// makes its enumerators visible on GCC; clang rejects the same file. See
// enumerators_of() below for the full account — it is the single best argument
// in this fork for CI running both toolchains.

// ── the public-API markers ──────────────────────────────────────────────────
//
// glib decorates every public declaration with a visibility macro on ITS OWN
// LINE, and that is a far better anchor than parsing C:
//
//     GLIB_AVAILABLE_IN_ALL
//     gchar *g_strdup (const gchar *str) G_GNUC_MALLOC;
//
// 4,400 of them across the four members. Anything without one is either
// private or a macro, and neither belongs in the export list.
inline bool is_api_marker(const std::string &line)
{
    static const char *pre[] = {"GLIB_", "GOBJECT_", "GMODULE_", "GIO_"};
    static const char *mid[] = {"AVAILABLE_IN", "AVAILABLE_TYPE_IN", "DEPRECATED",
                                "VAR", "AVAILABLE_STATIC_INLINE_IN",
                                "AVAILABLE_MACRO_IN", "AVAILABLE_ENUMERATOR_IN"};
    for (const char *p : pre) {
        if (line.compare(0, std::strlen(p), p) != 0) continue;
        for (const char *m : mid) {
            if (line.compare(std::strlen(p), std::strlen(m), m) == 0) return true;
        }
    }
    return false;
}

// A backstop. Every `using ::void;` the scanner has produced so far came from
// a specific parsing bug that is now fixed — but a keyword in the export list
// is a syntax error rather than a missing name, so it is worth refusing
// cheaply rather than discovering it on the next version bump.
inline bool is_reserved_word(const std::string &n)
{
    static const std::set<std::string> kw = {
        "void", "enum", "struct", "union", "const", "static", "inline", "extern",
        "typedef", "unsigned", "signed", "char", "short", "int", "long", "float",
        "double", "return", "sizeof", "volatile", "register", "auto", "class",
        "namespace", "template", "operator", "public", "private", "protected",
    };
    // Reserved to the implementation, and in these headers that means an
    // include guard: `__G_VARIANT_H__` reached the export list once.
    if (n.size() > 1 && n[0] == '_' && n[1] == '_') return true;
    return kw.count(n) != 0;
}

// The identifier a declaration declares: the last identifier that is followed
// by `(` (a function) or, failing that, the last identifier before `;` or `[`
// (a variable). Pointer stars, calling conventions and attribute macros all
// sit outside that, so this does not need to understand C types.
// ⚠️ glib PARENTHESISES A NAME TO DEFEND IT FROM MACRO EXPANSION:
//
//     void     (g_free)         (gpointer mem);
//
// The declarator is then at paren depth 1, and a depth-0 rule rejects it —
// which silently dropped g_free, g_string_free and the GVariant constructors
// from the export list while the module still compiled. Unwrap `(ident)` when
// what follows is a parameter list.
inline std::string unwrap_protective_parens(const std::string &in)
{
    std::string out;
    for (std::size_t i = 0; i < in.size();) {
        if (in[i] == '(') {
            std::size_t a = i + 1;
            while (a < in.size() && std::isspace(static_cast<unsigned char>(in[a]))) ++a;
            std::size_t b = a;
            while (b < in.size()
                   && (std::isalnum(static_cast<unsigned char>(in[b])) || in[b] == '_'))
                ++b;
            std::size_t c = b;
            while (c < in.size() && std::isspace(static_cast<unsigned char>(in[c]))) ++c;
            if (b > a && c < in.size() && in[c] == ')') {
                std::size_t d = c + 1;
                while (d < in.size() && std::isspace(static_cast<unsigned char>(in[d]))) ++d;
                if (d < in.size() && in[d] == '(') {
                    out += " " + in.substr(a, b - a) + " ";
                    i = c + 1;
                    continue;
                }
            }
        }
        out += in[i++];
    }
    return out;
}

inline std::string declared_name(const std::string &raw_decl)
{
    const std::string decl = unwrap_protective_parens(raw_decl);
    std::string best;
    // ⚠️ PARENTHESIS DEPTH 0 ONLY. A PARAMETER can look exactly like the thing
    // being declared:
    //
    //     void g_key_file_set_integer_list (GKeyFile *kf, …, gint list[], …);
    //                                                           ^^^^
    // `list[` matches "identifier followed by `[`" just as well as the
    // function name matches "identifier followed by `(`", and being later it
    // won. Parameters live inside the parens; the declarator does not.
    int paren = 0;
    for (std::size_t i = 0; i < decl.size();) {
        if (decl[i] == '(') { ++paren; ++i; continue; }
        if (decl[i] == ')') { --paren; ++i; continue; }
        if (!(std::isalpha(static_cast<unsigned char>(decl[i])) || decl[i] == '_')) {
            ++i;
            continue;
        }
        const std::size_t a = i;
        while (i < decl.size()
               && (std::isalnum(static_cast<unsigned char>(decl[i])) || decl[i] == '_'))
            ++i;
        const std::string id = decl.substr(a, i - a);
        std::size_t j = i;
        while (j < decl.size() && std::isspace(static_cast<unsigned char>(decl[j]))) ++j;
        // A name followed by `(` is either the function being declared or a
        // macro being invoked; glib's attribute macros are all upper case.
        const bool upper = id.find_first_of("abcdefghijklmnopqrstuvwxyz") == std::string::npos;
        if (paren == 0 && j < decl.size()
            && (decl[j] == '(' || decl[j] == ';' || decl[j] == '[') && !upper)
            best = id;
    }
    return best;
}

// ⚠️ A DECLARATION CAN END WITH AN ANNOTATION RATHER THAN ITS OWN NAME:
//
//     typedef enum { … } GModuleError
//     GMODULE_AVAILABLE_ENUMERATOR_IN_2_70;
//
//     #define G_MODULE_ERROR g_module_error_quark () GMODULE_AVAILABLE_MACRO_IN_2_70
//
// Taking "the last identifier" then yields the ANNOTATION — which exported
// `using ::GMODULE_AVAILABLE_ENUMERATOR_IN_2_70;` (a hard error, so it was
// caught) and, worse, LOST `GModuleError` (silent). This is the same shape as
// the bug in the gio enum scanner: `} GTlsRehandshakeMode
// GIO_DEPRECATED_TYPE_IN_2_60;`. Strip them before reading anything.
inline std::string strip_trailing_annotations(std::string s)
{
    for (;;) {
        std::size_t e = s.find_last_not_of(" \t\n;");
        if (e == std::string::npos) return s;
        std::size_t b = e;
        while (b > 0 && (std::isalnum(static_cast<unsigned char>(s[b - 1])) || s[b - 1] == '_'))
            --b;
        const std::string tail = s.substr(b, e - b + 1);
        if (!is_api_marker(tail)) return s;
        s.erase(b);
    }
}

// A typedef's name: the last identifier before the final `;` at depth 0.
// Handles `typedef struct _X X;`, `typedef enum { … } X;` and the
// function-pointer form `typedef R (*X) (…);`.
inline std::string typedef_name(const std::string &raw)
{
    // ⚠️ TRUNCATE AT THE DECLARATION'S OWN SEMICOLON FIRST. An earlier version
    // appended one and then searched for the last `;`, which lands past a
    // trailing comment — `typedef gint gatomicrefcount; /* … using atomics */`
    // exported `atomics`. Comments are stripped upstream of here now, but the
    // truncation is what makes that unnecessary to rely on.
    std::string cut = raw;
    {
        // ⚠️ AT BRACE DEPTH 0. `typedef struct { GTestLogType log_type; … }
        // GTestLogBuffer;` has its first `;` after a MEMBER, and truncating
        // there named the typedef `log_type`.
        int d = 0;
        char quote = 0;
        for (std::size_t i = 0; i < cut.size(); ++i) {
            const char c = cut[i];
            if (quote) {
                if (c == '\\') ++i;
                else if (c == quote) quote = 0;
                continue;
            }
            if (c == '\'' || c == '"') { quote = c; continue; }
            if (c == '{') ++d;
            else if (c == '}') --d;
            else if (c == ';' && d <= 0) { cut.erase(i); break; }
        }
    }
    const std::string s = strip_trailing_annotations(cut) + ";";
    const std::size_t end = s.rfind(';');
    if (end == std::string::npos) return {};
    // `typedef R (*X) (args);` — the name is inside the FIRST parenthesised
    // group, not before the semicolon.
    const std::size_t star = s.find("(*");
    if (star != std::string::npos) {
        std::size_t a = star + 2;
        while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
        std::size_t b = a;
        while (b < s.size()
               && (std::isalnum(static_cast<unsigned char>(s[b])) || s[b] == '_'))
            ++b;
        if (b > a) return s.substr(a, b - a);
    }
    std::size_t b = end;
    while (b > 0 && !(std::isalnum(static_cast<unsigned char>(s[b - 1])) || s[b - 1] == '_'))
        --b;
    std::size_t a = b;
    while (a > 0 && (std::isalnum(static_cast<unsigned char>(s[a - 1])) || s[a - 1] == '_'))
        --a;
    return b > a ? s.substr(a, b - a) : std::string{};
}

// ── the scan ────────────────────────────────────────────────────────────────
//
// One pass per header, tracking brace depth so that a marker or typedef inside
// a struct body is not mistaken for a top-level declaration.
struct module_names {
    std::set<std::string> exports;   // for src/<member>.cppm
    std::vector<std::string> macros; // verbatim #define blocks
};

// ⚠️⚠️ ENUMERATORS DO **NOT** COME FREE, AND THE TWO TOOLCHAINS DISAGREE.
//
// This generator shipped a version that exported only the typedef, on the
// strength of an mcpp end-to-end run: `GModuleFlags f = G_MODULE_BIND_LAZY;`
// compiled and ran. It compiled ON GCC. Clang rejects the same file —
//
//     error: use of undeclared identifier 'G_MODULE_BIND_LAZY'
//     error: use of undeclared identifier 'G_ZLIB_COMPRESSOR_FORMAT_ZLIB'
//     error: use of undeclared identifier 'G_UNICODE_OTHER_LETTER'
//
// — because a using-declaration naming an enumeration does not introduce its
// enumerators, and GCC's reachability is the lenient reading. The llvm leg of
// CI is what found it; one toolchain would have called this done.
//
// So every enumerator is exported by name. glib writes them as
// `typedef enum { A = 1, B, … } GName;`, so they are the first identifier of
// each comma-separated item inside the braces, minus the availability
// annotations glib sprinkles between them.
inline void enumerators_of(const std::string &block, std::set<std::string> &out)
{
    const std::size_t open = block.find('{');
    if (open == std::string::npos) return;
    if (block.compare(0, 7, "typedef") != 0) return;
    // Only an ENUM has enumerators; a struct's members are not names.
    const std::size_t kw = block.find("enum");
    if (kw == std::string::npos || kw > open) return;

    int d = 0;
    char quote = 0;
    std::string item;
    for (std::size_t i = open; i < block.size(); ++i) {
        const char c = block[i];
        if (quote) {
            if (c == '\\') ++i;
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '\'' || c == '"') { quote = c; continue; }
        if (c == '{') { ++d; if (d == 1) continue; }
        else if (c == '}') { --d; if (d == 0) { }
        }
        if (d == 0) break;
        if (c == ',') {
            // The enumerator is the FIRST identifier of the item; anything
            // after `=` is its value and anything upper-case-only that follows
            // is an availability annotation.
            std::size_t a = item.find_first_not_of(" \t\n");
            if (a != std::string::npos) {
                std::size_t b = a;
                while (b < item.size()
                       && (std::isalnum(static_cast<unsigned char>(item[b])) || item[b] == '_'))
                    ++b;
                const std::string id = item.substr(a, b - a);
                if (!id.empty() && !is_api_marker(id) && !is_reserved_word(id))
                    out.insert(id);
            }
            item.clear();
            continue;
        }
        item += c;
    }
    // The last item has no trailing comma.
    std::size_t a = item.find_first_not_of(" \t\n");
    if (a != std::string::npos) {
        std::size_t b = a;
        while (b < item.size()
               && (std::isalnum(static_cast<unsigned char>(item[b])) || item[b] == '_'))
            ++b;
        const std::string id = item.substr(a, b - a);
        if (!id.empty() && !is_api_marker(id) && !is_reserved_word(id)) out.insert(id);
    }
}

// ⚠️ A TEXT SCANNER CANNOT SEE THROUGH A DECLARATION MACRO.
//
//     G_DECLARE_INTERFACE (GListModel, g_list_model, G, LIST_MODEL, GObject)
//     G_DECLARE_FINAL_TYPE (GListStore, g_list_store, G, LIST_STORE, GObject)
//
// expand to the typedef, the class/interface struct AND `<prefix>_get_type`,
// none of which appears literally. gio uses them 12 times and gobject 17, and
// among the casualties were `GListModel`, `GListStore` and
// `g_list_model_get_type` — the exact symbols pango is waiting on. The module
// compiled without them, which is what makes this worth naming: the first
// consumer found it, not the build.
//
// The two names they always declare are the first two arguments; the third
// derived name is `<TypeName>Class` for a type and `<TypeName>Interface` for
// an interface.
inline void names_from_declare_macro(const std::string &t, std::set<std::string> &out)
{
    static const char *forms[] = {"G_DECLARE_INTERFACE", "G_DECLARE_FINAL_TYPE",
                                  "G_DECLARE_DERIVABLE_TYPE"};
    for (const char *f : forms) {
        if (t.compare(0, std::strlen(f), f) != 0) continue;
        const std::size_t open = t.find('(');
        if (open == std::string::npos) return;
        std::vector<std::string> arg;
        std::string cur;
        for (std::size_t i = open + 1; i < t.size() && arg.size() < 2; ++i) {
            if (t[i] == ',' || t[i] == ')') { arg.push_back(cur); cur.clear(); continue; }
            if (!std::isspace(static_cast<unsigned char>(t[i]))) cur += t[i];
        }
        if (arg.size() < 2 || arg[0].empty() || arg[1].empty()) return;
        out.insert(arg[0]);
        out.insert(arg[1] + "_get_type");
        out.insert(arg[0] + (std::strcmp(f, "G_DECLARE_INTERFACE") == 0 ? "Interface"
                                                                       : "Class"));
        return;
    }
}

// ⚠️ BRACES INSIDE LITERALS DO NOT NEST ANYTHING, AND ONE OF THEM COST 200
// NAMES. gvariant.h has
//
//     G_VARIANT_CLASS_DICT_ENTRY    = '{'
//
// inside `typedef enum { … } GVariantClass;`. Counting it left the typedef
// reader one brace short of closing, so it kept calling getline and SWALLOWED
// THE REST OF THE FILE — every `g_variant_*` function vanished from the export
// list while the module still compiled and the other 1,853 names made it look
// complete. The consumer found it: `'g_variant_new_int32' was not declared`.
inline void count_braces(const std::string &s, int &d)
{
    char quote = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (quote) {
            if (c == '\\') { ++i; continue; }   // an escape hides the next char
            if (c == quote) quote = 0;
            continue;
        }
        if (c == '\'' || c == '"') { quote = c; continue; }
        if (c == '{') ++d;
        else if (c == '}') --d;
    }
}

// ⚠️ THE SCANNER MUST DO TWO THINGS A GREP DOES NOT: strip comments, and
// respect platform guards. Both were found by the compiler rather than by
// reading, and they fail differently:
//
//   comments      `typedef gint gatomicrefcount; /* … using atomics */`
//                 exported `atomics` — a hard error, so it was caught. Prose
//                 in a header is not declarations.
//   #ifdef        `g_win32_*`, `g_io_channel_win32_*` and `GWin32OSType` are
//                 declared under `#ifdef G_OS_WIN32`. Exporting them on Linux
//                 is 18 hard errors. This package is Linux-only, so the guard
//                 is read rather than evaluated: anything under a Windows or
//                 Cocoa condition is skipped.
inline bool is_foreign_platform_guard(const std::string &t)
{
    // ⚠️ VERSION CONDITIONS ARE NOT INACTIVE. `#if GLIB_VERSION_MAX_ALLOWED >=
    // GLIB_VERSION_2_58` guards REAL API, and skipping those regions silently
    // dropped g_free, g_string_free and the whole GVariant family from the
    // export list — the module compiled and the first consumer did not. Only
    // conditions that name another platform or a configuration this package
    // does not build belong here.

    static const char *bad[] = {
        // Other platforms.
        "G_OS_WIN32", "_WIN32", "_MSC_VER", "G_PLATFORM_WIN32",
        "__APPLE__", "G_OS_DARWIN", "HAVE_COCOA", "__OS2__",
        // Build configurations this package does not select. Each declares
        // real API that is simply not compiled, so exporting it is an
        // undefined reference waiting to happen:
        //   G_ENABLE_DEBUG   g_slice_debug_tree_statistics
        //   __GI_SCANNER__   GObject-Introspection's scanner, never a build
        "G_ENABLE_DEBUG", "__GI_SCANNER__",
    };
    for (const char *b : bad)
        if (t.find(b) != std::string::npos) return true;
    return false;
}

// ⚠️ BRACE DEPTH IS NOT USED AS A GATE, and that is deliberate. An earlier
// version required `depth == 0` before believing a marker or a typedef. One
// unbalanced-looking brace anywhere above — inside a macro body, a doc example,
// a construct the counter did not model — sticks the depth at 1 and SILENTLY
// discards the rest of the file. Measured: gvariant.h sat at depth 1 from line
// 60 onward, so every `g_variant_new_*` was missing from the module while it
// still compiled and 1,826 other names made it look complete.
//
// The marker is a strong enough signal on its own: glib never decorates a
// struct member with GLIB_AVAILABLE_IN_*, and `typedef` at the start of a line
// is not ambiguous either. Trusting them removes the whole class.
// ⚠️ COMMENTS ARE STRIPPED ONCE, FOR THE WHOLE FILE, BEFORE ANYTHING READS IT.
//
// Doing it inline in the main loop was not enough: the typedef reader below
// has its own getline loop and never saw the stripper, so the script-code
// comments inside `typedef enum { … } GUnicodeScript;` —
//
//     G_UNICODE_SCRIPT_GEORGIAN,   /* Geor */
//
// became exported names, and clang said `no member named 'Geoa' in the global
// namespace`. Two readers over one stream is the bug; one buffer is the fix.
inline std::string strip_comments(const fs::path &h)
{
    std::ifstream in(h);
    std::string all((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
    std::string out;
    out.reserve(all.size());
    bool block = false;
    char quote = 0;
    for (std::size_t i = 0; i < all.size(); ++i) {
        const char c = all[i];
        if (block) {
            if (c == '*' && i + 1 < all.size() && all[i + 1] == '/') { block = false; ++i; }
            else if (c == '\n') out += '\n';   // keep line structure
            continue;
        }
        if (quote) {
            out += c;
            if (c == '\\' && i + 1 < all.size()) { out += all[++i]; continue; }
            if (c == quote) quote = 0;
            continue;
        }
        if (c == '\'' || c == '"') { quote = c; out += c; continue; }
        if (c == '/' && i + 1 < all.size() && all[i + 1] == '*') { block = true; ++i; out += ' '; continue; }
        if (c == '/' && i + 1 < all.size() && all[i + 1] == '/') {
            while (i < all.size() && all[i] != '\n') ++i;
            out += '\n';
            continue;
        }
        out += c;
    }
    return out;
}

inline void scan_header_for_module(const fs::path &h, module_names &out)
{
    const std::string body = strip_comments(h);
    std::istringstream in(body);
    std::string line;
    bool pending_decl = false;
    std::string decl;

    // Nesting depth of `#if`, and the depth at which a foreign-platform guard
    // opened (0 = not inside one). `#else` of such a guard is OUR branch, so
    // the skip ends there.
    int cond_depth = 0, skip_from = 0;
    bool closing_skip = false;

    while (std::getline(in, line)) {
        // Strip a trailing \r so a CRLF checkout does not defeat every compare.
        if (!line.empty() && line.back() == '\r') line.pop_back();


        // ── platform guards ─────────────────────────────────────────────────
        {
            std::string c = line;
            const std::size_t p0 = c.find_first_not_of(" \t");
            if (p0 != std::string::npos) c = c.substr(p0);
            if (c.rfind("#if", 0) == 0) {
                ++cond_depth;
                if (!skip_from && is_foreign_platform_guard(c)) skip_from = cond_depth;
            } else if (c.rfind("#elif", 0) == 0) {
                if (skip_from == cond_depth && !is_foreign_platform_guard(c)) skip_from = 0;
                else if (!skip_from && is_foreign_platform_guard(c)) skip_from = cond_depth;
            } else if (c.rfind("#else", 0) == 0) {
                if (skip_from == cond_depth) { skip_from = 0; closing_skip = true; }
            } else if (c.rfind("#endif", 0) == 0) {
                if (skip_from == cond_depth) { skip_from = 0; closing_skip = true; }
                if (cond_depth > 0) --cond_depth;
            }
        }
        // ⚠️ THE LINE THAT CLOSES A SKIPPED REGION IS ITSELF SKIPPED. Its `#if`
        // was never emitted, so emitting the `#endif` gives
        // `'#endif' without '#if'` in the macro header.
        if (closing_skip) { closing_skip = false; continue; }
        if (skip_from) continue;

        std::string t = line;
        const std::size_t ns = t.find_first_not_of(" \t");
        if (ns != std::string::npos) t = t.substr(ns);

        // ── macros ──────────────────────────────────────────────────────────
        if (t.rfind("#define", 0) == 0) {
            std::string block = line;
            // A `\`-continued macro is one logical line.
            while (!block.empty() && block.back() == '\\' && std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                block += "\n" + line;
            }
            // The name, so include guards and internals can be dropped.
            std::size_t a = block.find("#define") + 7;
            while (a < block.size() && std::isspace(static_cast<unsigned char>(block[a]))) ++a;
            std::size_t b = a;
            while (b < block.size()
                   && (std::isalnum(static_cast<unsigned char>(block[b])) || block[b] == '_'))
                ++b;
            const std::string name = block.substr(a, b - a);
            // ⚠️ STRIP THE ANNOTATION FROM THE BODY, NEVER FROM THE NAME.
            // `#define GLIB_DEPRECATED_MACRO` IS an annotation by name, and
            // stripping blindly produced a bare `#define ` — "no macro name
            // given in '#define' directive". Cut past the name (and past a
            // parameter list, if any) before touching anything.
            {
                std::size_t body = b;
                if (body < block.size() && block[body] == '(') {
                    int d = 0;
                    for (; body < block.size(); ++body) {
                        if (block[body] == '(') ++d;
                        else if (block[body] == ')') { --d; if (d == 0) { ++body; break; } }
                    }
                }
                block = block.substr(0, body)
                        + strip_trailing_annotations(block.substr(body));
            }
            const bool guard = name.size() > 2 && name.compare(0, 2, "__") == 0;
            const bool internal = !name.empty() && name[0] == '_';
            const bool is_guard_tail = name.find("_H__") != std::string::npos
                                       || name.find("_H_") != std::string::npos;
            // The annotation families are emitted EMPTY in the preamble; a
            // projected copy would drag their version machinery with it.
            const bool annotation = is_api_marker(name)
                                    || name.rfind("GLIB_VERSION_", 0) == 0;
            if (!name.empty() && !guard && !internal && !is_guard_tail && !annotation)
                out.macros.push_back(block);
            continue;
        }

        // ⚠️ KEEP THE CONDITIONAL STRUCTURE. Dumping every `#define` flat gives
        // BOTH branches of `#ifdef GLIB_DISABLE_DEPRECATION_WARNINGS`, so the
        // second silently redefines the first — and flattening the version
        // macros tripped gversionmacros.h's own sanity check:
        //
        //   #error "GLIB_VERSION_MIN_REQUIRED must be <= GLIB_VERSION_CUR_STABLE"
        //
        // The macro layer is only faithful if its `#if`s come with it.
        // ⚠️ CONDITIONALS ARE NOT PROJECTED, and the reason is structural: the
        // typedef reader below consumes lines with its own getline loop, so it
        // bypasses this one — the two disagree about nesting and the output
        // came out with `'#endif' without '#if'`. With the version and
        // deprecation machinery already excluded, what remains is
        // unconditional in a default Linux build.

        if (t.rfind("#", 0) == 0) continue;  // other preprocessor lines

        // ── a marked declaration ────────────────────────────────────────────
        // ⚠️ `GLIB_VAR const guint glib_major_version;` PUTS THE MARKER ON THE
        // DECLARATION'S OWN LINE. Requiring the marker to stand alone skipped
        // every exported variable — glib_major_version among them.
        if (t.compare(0, 10, "G_DECLARE_") == 0) {
            names_from_declare_macro(t, out.exports);
            continue;
        }

        if (is_api_marker(t) && t.find(';') != std::string::npos) {
            const std::string n = declared_name(t);
            if (!n.empty() && !is_reserved_word(n)) out.exports.insert(n);
            continue;
        }

        if (is_api_marker(t) && t.find(';') == std::string::npos
            && t.find('(') == std::string::npos) {
            pending_decl = true;
            decl.clear();
            continue;
        }

        // ⚠️ A MARKER CAN INTRODUCE A TYPEDEF, and routing that through the
        // declaration path is wrong in BOTH directions:
        //
        //     GLIB_AVAILABLE_TYPE_IN_2_72
        //     typedef enum { … } GMainContextFlags;     -> exported `enum`
        //
        //     GLIB_AVAILABLE_TYPE_IN_2_64
        //     typedef void (*GSourceDisposeFunc) (…);   -> exported `void`
        //
        // The bogus name is a hard error, so it is caught. The REAL name being
        // lost is silent, and that is the half that matters: without this,
        // GMainContextFlags, GSourceDisposeFunc and GUnixPipe were missing
        // from the module while it compiled perfectly.
        if (pending_decl && t.rfind("typedef", 0) == 0) {
            pending_decl = false;
            // fall through to the typedef branch below
        }
        if (pending_decl) {
            decl += " " + t;
            // ⚠️ A MARKER CAN INTRODUCE A DEFINITION, NOT A DECLARATION:
            //
            //     GLIB_AVAILABLE_STATIC_INLINE_IN_2_70
            //     static inline int
            //     g_steal_fd (int *fd_ptr)
            //     {
            //       int fd = *fd_ptr;      <-- the first `;` is in the BODY
            //
            // Stopping at the first `;` named the function `fd_ptr`. And a
            // `static inline` has INTERNAL LINKAGE, so `using ::` on it is
            // TU-local exposure — a hard error, not a warning. Both are handled
            // by cutting at the brace and dropping anything static.
            const std::size_t brace = decl.find('{');
            const std::size_t semi = decl.find(';');
            if (brace != std::string::npos && (semi == std::string::npos || brace < semi)) {
                const std::string head = decl.substr(0, brace);
                if (head.find("static") == std::string::npos) {
                    const std::string n = declared_name(head + ";");
                    if (!n.empty() && !is_reserved_word(n)) out.exports.insert(n);
                }
                pending_decl = false;
                continue;
            }
            if (semi != std::string::npos) {
                if (decl.find("static") == std::string::npos) {
                    const std::string n = declared_name(decl);
                    if (!n.empty() && !is_reserved_word(n)) out.exports.insert(n);
                }
                pending_decl = false;
            }
            // A declaration that runs away is abandoned rather than guessed at.
            if (decl.size() > 4000) pending_decl = false;
            continue;
        }

        // ── typedefs ────────────────────────────────────────────────────────
        if (t.rfind("typedef", 0) == 0) {
            std::string block = t;
            int d = 0;
            count_braces(t, d);
            while ((d > 0 || block.find(';') == std::string::npos)
                   && std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                block += " " + line;
                count_braces(line, d);
                if (block.size() > 200000) break;
            }
            const std::string n = typedef_name(block);
            if (!n.empty() && !is_reserved_word(n)) out.exports.insert(n);
            enumerators_of(block, out.exports);
            continue;
        }

    }
}

// The public header set, derived from the umbrella header rather than
// transcribed. `glib.h` names 73 headers, `glib-object.h` 18, `gio.h` 152 — a
// hand-written list would be a version bump away from being wrong, which is
// the same reason the export list itself is generated.
//
// ⚠️ TWO SEARCH ROOTS. Some of what an umbrella includes is GENERATED —
// `<gobject/glib-enumtypes.h>` is written by this member's build.mcpp — so a
// derivation that looked only at upstream/ would silently drop it and the
// module would be missing every enum GType.
inline std::vector<fs::path> headers_from_umbrella(const fs::path &umbrella,
                                                   const std::string &subdir,
                                                   const std::vector<fs::path> &roots)
{
    std::vector<fs::path> out;
    std::set<std::string> seen;
    std::ifstream in(umbrella);
    std::string line;
    const std::string open = "<" + subdir + "/";
    while (std::getline(in, line)) {
        const std::size_t a = line.find(open);
        if (a == std::string::npos) continue;
        const std::size_t b = line.find('>', a);
        if (b == std::string::npos) continue;
        const std::string n = line.substr(a + open.size(), b - a - open.size());
        if (n.size() < 3 || n.compare(n.size() - 2, 2, ".h") != 0) continue;
        if (!seen.insert(n).second) continue;
        for (const auto &r : roots) {
            if (fs::exists(r / n)) { out.push_back(r / n); break; }
        }
    }
    // The umbrella itself declares things in a few cases, and costs nothing.
    if (fs::exists(umbrella)) out.push_back(umbrella);
    return out;
}

// ── the two outputs ─────────────────────────────────────────────────────────
//
// `umbrella` is what the module's global-module fragment includes; `headers`
// is the set scanned for names. They differ because the umbrella pulls in more
// than the member owns — gio.h reaches glib's headers too — and a member must
// export only ITS OWN names or two members would export the same one and a
// consumer naming both would get an ambiguity.
// ⚠️ A MEMBER RE-EXPORTS WHAT ITS HEADER INCLUDES, and that is not a nicety.
//
// `gmodule.h` includes `glib.h`, so `#include <gmodule.h>` hands a consumer
// `GQuark`. The module must do the same — and a consumer CANNOT get it any
// other way, because glib is a workspace PATH dependency of the other three:
//
//     error: dependency 'gnome.glib' is requested as both a version dep
//            (by 'your-package') and a path dep (by 'gnome.gmodule')
//
// So `import gnome.gmodule;` has to be as complete as `#include <gmodule.h>`.
// Measured first as a failure: `'GQuark' was not declared in this scope`, from
// a consumer that had done nothing wrong.
//
// Re-exporting the same module from two members is fine — a consumer naming
// gobject and gmodule imports one gnome.glib, not two.
inline void gen_module(const std::string &module_name,
                       const std::string &umbrella,
                       const std::vector<fs::path> &headers,
                       const fs::path &cppm_out,
                       const fs::path &macros_out,
                       const std::string &macro_header_spelling,
                       std::size_t min_exports,
                       const std::vector<std::string> &reexports = {},
                       const std::vector<std::string> &macro_includes = {})
{
    module_names n;
    for (const auto &h : headers) {
        scan_header_for_module(h, n);
        mcpp::rerun_if_changed(h.string().c_str());
    }

    if (n.exports.size() < min_exports) {
        note("%s: only %zu names to export, expected at least %zu — the scan\n"
             "stopped understanding something, and a short list is a module\n"
             "that compiles while missing API.\n",
             module_name.c_str(), n.exports.size(), min_exports);
        std::exit(1);
    }

    std::error_code ec;
    fs::create_directories(cppm_out.parent_path(), ec);
    fs::create_directories(macros_out.parent_path(), ec);

    {
        std::ofstream o(cppm_out);
        o << "// " << module_name << ", as a C++23 module.\n"
          << "//\n"
          << "// GENERATED by build.mcpp from upstream's public headers. Do not edit:\n"
          << "// a version bump regenerates it, so a name upstream added or removed\n"
          << "// cannot be silently missed.\n"
          << "//\n"
          << "// ⚠️ A MODULE CANNOT CARRY MACROS, and glib's macros are half its API.\n"
          << "// Pair this with <" << macro_header_spelling << ">.\n"
          << "module;\n"
          << "#include <" << umbrella << ">\n"
          << "export module " << module_name << ";\n"
          << "\n";
        if (!reexports.empty()) {
            o << "// This member's header includes theirs, so its module exports theirs.\n"
              << "// A consumer cannot name them itself: they are workspace PATH\n"
              << "// dependencies, and mcpp refuses a package requested both ways.\n";
            for (const auto &r : reexports) o << "export import " << r << ";\n";
            o << "\n";
        }
        o << "\n"
          << "// " << n.exports.size() << " names.\n"
          << "//\n"
          << "// Enumerators are NOT listed: glib writes `typedef enum { … } GName;`,\n"
          << "// and exporting the typedef of an unnamed enum makes its enumerators\n"
          << "// visible to importers. Measured through mcpp end to end.\n"
          << "export {\n";
        for (const auto &e : n.exports) o << "using ::" << e << ";\n";
        o << "}\n";
    }

    note("%s: %zu exports\n", module_name.c_str(), n.exports.size());
}
