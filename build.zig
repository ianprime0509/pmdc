const PmdFeature = enum {
    va,
    board2,
    adpcm,
    ademu,
    pcm,
    ppz,
    sync,
    vsync,
};

// The PMD variants and their features, mirroring the original variant entry
// files (PMD.ASM, PMD86.ASM, PMDB2.ASM, PMDPPZ.ASM, PMDPPZE.ASM, PMDVA.ASM,
// PMDVA1.ASM).
const pmd_variants = [_]struct {
    name: []const u8,
    features: []const PmdFeature,
}{
    .{ .name = "pmd", .features = &.{} },
    .{ .name = "pmd86", .features = &.{ .board2, .pcm } },
    .{ .name = "pmdb2", .features = &.{ .board2, .adpcm } },
    .{ .name = "pmdppz", .features = &.{ .board2, .adpcm, .ppz } },
    .{ .name = "pmdppze", .features = &.{ .board2, .adpcm, .ademu, .ppz } },
    .{ .name = "pmdva", .features = &.{ .va, .board2, .adpcm } },
    .{ .name = "pmdva1", .features = &.{.va} },
};

const cflags: []const []const u8 = &.{
    // Zig suppresses warnings from clang, so -Werror is the only way to
    // get them.
    "-Werror",
    "-Wall",
    "-Wextra",
    "-pedantic",
    "-std=c11",
};

// The port's C++ sources are held to the same warning level as the C ones.
// -pedantic is left out because pmd.h's anonymous structs, which are standard C,
// are a GNU extension in C++.
const cppflags: []const []const u8 = &.{
    "-Werror",
    "-Wall",
    "-Wextra",
    "-std=c++14",
};

// The PMD feature flags are defined for the whole module, but the YMFM sources
// must see neither the ones that collide with C++ library identifiers (`pcm`,
// `sync`) nor the rest of the port's configuration.
const ymfm_cppflags: []const []const u8 = &.{
    "-std=c++14",
    "-Upcm",
    "-Usync",
    "-Uvsync",
    "-Uva",
    "-Uboard2",
    "-Uadpcm",
    "-Uademu",
    "-Uppz",
};

fn addFeatures(mod: *std.Build.Module, features: []const PmdFeature) void {
    for (features) |feature| {
        mod.addCMacro(@tagName(feature), "1");
    }
}

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const lang = b.option([]const u8, "lang", "Language for strings in the compiled programs") orelse "jp";
    if (lang.len != 2) @panic("lang must be a two-character language code");
    const lang_num: u16 = 256 * @as(u16, lang[0]) + lang[1];

    const mc_exe = addMc(b, "mc", target, optimize, lang_num);
    b.installArtifact(mc_exe);
    const mch_exe = addMc(b, "mch", target, optimize, lang_num);
    mch_exe.root_module.addCMacro("hyouka", "1");
    b.installArtifact(mch_exe);
    const efc_exe = addMc(b, "efc", target, optimize, lang_num);
    efc_exe.root_module.addCMacro("efc", "1");
    b.installArtifact(efc_exe);

    const miniaudio_lib = addMiniaudio(b, target, optimize);

    const test_step = b.step("test", "Run all tests");

    const pmd_test_step = b.step("test-pmd", "Run PMD tests");
    for (pmd_variants) |variant| {
        const pmd_exe = addPmd(b, variant.name, target, optimize, lang_num, "pmd_main.c", null);
        addFeatures(pmd_exe.root_module, variant.features);
        b.installArtifact(pmd_exe);

        // The playback utility is installed once per variant, so the effect of
        // each variant's feature flags can be heard.
        const pmd_play_exe = addPmd(
            b,
            b.fmt("{s}-play", .{variant.name}),
            target,
            optimize,
            lang_num,
            "pmd_play.c",
            miniaudio_lib,
        );
        addFeatures(pmd_play_exe.root_module, variant.features);
        b.installArtifact(pmd_play_exe);

        // Run the unit tests and the header self-containment check for every
        // variant's flag set.
        const pmd_test_exe = addPmdTest(b, variant.name, target, optimize, lang_num);
        addFeatures(pmd_test_exe.root_module, variant.features);
        const pmd_test_run = b.addRunArtifact(pmd_test_exe);
        pmd_test_run.has_side_effects = true;
        pmd_test_run.addCheck(.{ .expect_term = .{ .exited = 0 } });
        pmd_test_step.dependOn(&pmd_test_run.step);

        const header_check_mod = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libc = true,
        });
        addFeatures(header_check_mod, variant.features);
        header_check_mod.addIncludePath(b.path("src/pmd"));
        header_check_mod.addCSourceFiles(.{
            .root = b.path("test/pmd"),
            .files = &.{"header.c"},
            .flags = cflags,
        });
        const header_check = b.addObject(.{
            .name = b.fmt("pmd-header-check-{s}", .{variant.name}),
            .root_module = header_check_mod,
        });
        pmd_test_step.dependOn(&header_check.step);
    }

    // sync and vsync are not features of any shipped variant, but the timer
    // paths they guard must still be compiled and exercised.
    const extra_test_features = [_]struct {
        name: []const u8,
        features: []const PmdFeature,
    }{
        .{ .name = "sync", .features = &.{.sync} },
        .{ .name = "vsync", .features = &.{.vsync} },
    };
    for (extra_test_features) |extra| {
        const pmd_test_exe = addPmdTest(b, extra.name, target, optimize, lang_num);
        addFeatures(pmd_test_exe.root_module, extra.features);
        const pmd_test_run = b.addRunArtifact(pmd_test_exe);
        pmd_test_run.has_side_effects = true;
        pmd_test_run.addCheck(.{ .expect_term = .{ .exited = 0 } });
        pmd_test_step.dependOn(&pmd_test_run.step);
    }

    const mc_test_harness_mod = b.createModule(.{
        .root_source_file = b.path("test/mc/harness.zig"),
        .target = b.graph.host,
        .optimize = .Debug,
    });
    const mc_test_harness_exe = b.addExecutable(.{
        .name = "mc-test-harness",
        .root_module = mc_test_harness_mod,
    });

    const mc_test_run = b.addRunArtifact(mc_test_harness_exe);
    mc_test_run.addFileArg(mc_exe.getEmittedBin());
    mc_test_run.addDirectoryArg(b.path("test/mc/cases"));
    mc_test_run.addCheck(.{ .expect_term = .{ .exited = 0 } });
    // This ensures the tests are always re-run, even if test cases are changed.
    // Zig's cache system doesn't look at the contents of the test cases
    // directory when deciding if a cached result can be used.
    mc_test_run.has_side_effects = true;

    const mc_test_step = b.step("test-mc", "Run MC tests");
    mc_test_step.dependOn(&mc_test_run.step);
    test_step.dependOn(mc_test_step);
    test_step.dependOn(pmd_test_step);
}

fn addMc(
    b: *std.Build,
    name: []const u8,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    lang_num: u16,
) *std.Build.Step.Compile {
    const mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    mod.addCMacro("lang", b.fmt("{}", .{lang_num}));
    mod.addCSourceFiles(.{
        .root = b.path("src/mc"),
        .files = &.{
            "mc.c",
            "mc_main.c",
            "mc_stdio.c",
        },
        .flags = cflags,
    });

    return b.addExecutable(.{
        .name = name,
        .root_module = mod,
    });
}

fn addPmd(
    b: *std.Build,
    name: []const u8,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    lang_num: u16,
    entry: []const u8,
    miniaudio_lib: ?*std.Build.Step.Compile,
) *std.Build.Step.Compile {
    const mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
        .link_libcpp = true,
    });
    mod.addCMacro("lang", b.fmt("{}", .{lang_num}));
    addYmfm(b, mod);
    const sources = [_][]const u8{ "pmd.c", entry, "pmd_stdio.c" };
    mod.addCSourceFiles(.{
        .root = b.path("src/pmd"),
        .files = &sources,
        .flags = cflags,
    });
    mod.addCSourceFiles(.{
        .root = b.path("src/pmd"),
        .files = &.{"pmd_ymfm.cpp"},
        .flags = cppflags,
    });
    if (miniaudio_lib) |lib| {
        mod.addIncludePath(b.dependency("miniaudio", .{}).path("."));
        mod.linkLibrary(lib);
    }

    return b.addExecutable(.{
        .name = name,
        .root_module = mod,
    });
}

// miniaudio is a single-file library. It is built once and linked into each
// playback executable.
fn addMiniaudio(
    b: *std.Build,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
) *std.Build.Step.Compile {
    const miniaudio = b.dependency("miniaudio", .{});
    const mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
        // miniaudio is third-party code and its sample conversion shifts
        // negative values, which the Debug-mode C sanitizer treats as fatal.
        .sanitize_c = .off,
    });
    mod.addCSourceFiles(.{
        .root = miniaudio.path("."),
        .files = &.{"miniaudio.c"},
        .flags = &.{
            "-std=c89",
            "-Wall",
            "-Wextra",
            "-pedantic",
        },
    });

    return b.addLibrary(.{
        .name = "miniaudio",
        .root_module = mod,
    });
}

// YMFM is a source-only C++ package, so the backend's sources are compiled
// straight into the PMD module instead of going through a Zig build script.
fn addYmfm(b: *std.Build, mod: *std.Build.Module) void {
    const ymfm = b.dependency("ymfm", .{});
    mod.addSystemIncludePath(ymfm.path("src"));
    mod.addCSourceFiles(.{
        .root = ymfm.path("src"),
        .files = &.{
            "ymfm_adpcm.cpp",
            "ymfm_misc.cpp",
            "ymfm_opn.cpp",
            "ymfm_ssg.cpp",
        },
        .flags = ymfm_cppflags,
    });
}

fn addPmdTest(
    b: *std.Build,
    name: []const u8,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    lang_num: u16,
) *std.Build.Step.Compile {
    const mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
        .link_libcpp = true,
    });
    mod.addCMacro("lang", b.fmt("{}", .{lang_num}));
    mod.addIncludePath(b.path("src/pmd"));
    addYmfm(b, mod);
    mod.addCSourceFiles(.{
        .root = b.path("src/pmd"),
        .files = &.{
            "pmd.c",
            "pmd_stdio.c",
        },
        .flags = cflags,
    });
    mod.addCSourceFiles(.{
        .root = b.path("src/pmd"),
        .files = &.{"pmd_ymfm.cpp"},
        .flags = cppflags,
    });
    mod.addCSourceFiles(.{
        .root = b.path("test/pmd"),
        .files = &.{"test_pmd.c"},
        .flags = cflags,
    });

    return b.addExecutable(.{
        .name = b.fmt("pmd-test-{s}", .{name}),
        .root_module = mod,
    });
}

const std = @import("std");
