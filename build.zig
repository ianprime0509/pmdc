const std = @import("std");

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

    const test_step = b.step("test", "Run all tests");

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
    mc_test_run.addCheck(.{ .expect_term = .{ .Exited = 0 } });
    // This ensures the tests are always re-run, even if test cases are changed.
    // Zig's cache system doesn't look at the contents of the test cases
    // directory when deciding if a cached result can be used.
    mc_test_run.has_side_effects = true;

    const mc_test_step = b.step("test-mc", "Run MC tests");
    mc_test_step.dependOn(&mc_test_run.step);
    test_step.dependOn(mc_test_step);
}

fn addMc(
    b: *std.Build,
    name: []const u8,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    lang_num: u16,
) *std.Build.Step.Compile {
    const cflags: []const []const u8 = &.{
        "-Wall",
        "-Wextra",
        "-pedantic",
        "-std=c11",
    };

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
