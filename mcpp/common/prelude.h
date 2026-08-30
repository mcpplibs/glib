#pragma once
// Shared by the three build programs: set up the paths and the common
// generators, so each member's build.mcpp is only the list of what IT needs.
//
// ⚠️ The FOUR shared outputs — config.h, glibconfig.h, glib/gversionmacros.h,
// glib/glib-visibility.h — are produced by every member into its OWN include/.
// glib's headers are what everything includes, so every member needs them; and
// a member that generates what it compiles cannot be caught by the failure
// described at the top of generators.h.
inline void mcpp_glib_common_setup()
{
    namespace fs = std::filesystem;
    g_root = fs::path(mcpp::manifest_dir());
    g_up   = (g_root / ".." / ".." / "upstream").lexically_normal();
    g_inc  = (g_root / "include").lexically_normal();

    std::error_code ec;
    // Wiped so a header that stopped being generated stops shadowing too. The
    // DIRECTORY survives with its .gitkeep, because mcpp reads `include_dirs`
    // before running this program and silently drops an entry that is not there.
    for (const char *sub : {"glib", "gobject", "gmodule"})
        fs::remove_all(g_inc / sub, ec);
    for (const char *f : {"config.h", "glibconfig.h", "gmoduleconf.h"})
        fs::remove(g_inc / f, ec);
    fs::create_directories(g_inc, ec);

    gen_version_macros();
    gen_visibility("GLIB", g_inc / "glib" / "glib-visibility.h");
    gen_glibconfig();
    gen_config();
}
