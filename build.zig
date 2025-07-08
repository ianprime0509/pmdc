const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const lang = b.option([]const u8, "lang", "Language for strings in the compiled programs") orelse "jp";
    if (lang.len != 2) @panic("lang must be a two-character language code");
    const lang_num: u16 = 256 * @as(u16, lang[0]) + lang[1];

    const mc_exe = addMc(b, "mc", target, optimize, lang_num);
    b.installArtifact(mc_exe);
    // TODO: add these
    // const mch_exe = addMc(b, "mch", target, optimize, lang_num);
    // mch_exe.root_module.addCMacro("hyouka", "1");
    // b.installArtifact(mch_exe);
    // const efc_exe = addMc(b, "efc", target, optimize, lang_num);
    // efc_exe.root_module.addCMacro("efc", "1");
    // b.installArtifact(efc_exe);

    const mc_run = b.addRunArtifact(mc_exe);
    mc_run.step.dependOn(b.getInstallStep());
    if (b.args) |args| mc_run.addArgs(args);

    const mc_run_step = b.step("mc", "Run mc");
    mc_run_step.dependOn(&mc_run.step);
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
