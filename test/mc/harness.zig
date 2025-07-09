const std = @import("std");
const log = std.log;

pub fn main() !u8 {
    const gpa = std.heap.smp_allocator;

    const args = try std.process.argsAlloc(gpa);
    defer std.process.argsFree(gpa, args);
    if (args.len != 3) {
        log.err("usage: harness MC-PATH TEST-DIR", .{});
        return 1;
    }
    const mc_path = args[1];
    const test_dir_path = args[2];

    var results: Results = .init;
    var test_dir = try std.fs.cwd().openDir(test_dir_path, .{ .iterate = true });
    defer test_dir.close();
    var test_dir_iterator = test_dir.iterate();
    while (try test_dir_iterator.next()) |test_dir_entry| {
        if (std.mem.endsWith(u8, test_dir_entry.name, ".MML")) {
            try runTest(gpa, test_dir_entry.name, test_dir, mc_path, &results);
        }
    }
    log.info("pass: {}, fail: {}", .{ results.n_pass, results.n_fail });
    return if (results.n_fail == 0) 0 else 1;
}

fn runTest(
    gpa: std.mem.Allocator,
    mml_path: []const u8,
    test_dir: std.fs.Dir,
    mc_path: []const u8,
    results: *Results,
) !void {
    const mml = try test_dir.readFileAlloc(gpa, mml_path, std.math.maxInt(u16));
    defer gpa.free(mml);
    var m_path_option: ?[]const u8 = null;
    var console_path_option: ?[]const u8 = null;
    var mml_lines = std.mem.splitSequence(u8, mml, "\r\n");
    while (mml_lines.next()) |line| {
        if (!std.mem.startsWith(u8, line, ";")) break;
        if (std.mem.startsWith(u8, line, "; Output: ")) {
            m_path_option = line["; Output: ".len..];
        } else if (std.mem.startsWith(u8, line, "; Console: ")) {
            console_path_option = line["; Console: ".len..];
        }
    }

    var run_dir = std.testing.tmpDir(.{});
    defer run_dir.cleanup();
    try run_dir.dir.writeFile(.{
        .sub_path = mml_path,
        .data = mml,
    });

    const m_path = m_path_option orelse
        return results.fail(mml_path, "missing output file comment", .{});
    const console_path = console_path_option orelse
        return results.fail(mml_path, "missing console output file comment", .{});

    const m_data = try test_dir.readFileAlloc(gpa, m_path, std.math.maxInt(u16));
    defer gpa.free(m_data);
    const console_data = try test_dir.readFileAlloc(gpa, console_path, std.math.maxInt(u16));
    defer gpa.free(console_data);

    var env_map: std.process.EnvMap = .init(gpa);
    defer env_map.deinit();
    const process_output = try std.process.Child.run(.{
        .allocator = gpa,
        .argv = &.{ mc_path, mml_path },
        .cwd_dir = run_dir.dir,
        .env_map = &env_map,
        .max_output_bytes = 4096,
    });
    defer gpa.free(process_output.stdout);
    defer gpa.free(process_output.stderr);

    switch (process_output.term) {
        .Exited => |status| if (status != 0) return results.fail(mml_path, "compiler exited with status {}", .{status}),
        else => |term| return results.fail(mml_path, "compiler exited abnormally: {}", .{term}),
    }

    const actual_m_data = run_dir.dir.readFileAlloc(gpa, m_path, std.math.maxInt(u16)) catch |err| switch (err) {
        error.FileNotFound => return results.fail(mml_path, "expected output file '{s}' not produced", .{m_path}),
        else => |other_err| return other_err,
    };
    defer gpa.free(actual_m_data);

    if (!std.mem.eql(u8, m_data, actual_m_data)) {
        std.debug.print("expected output:\n", .{});
        hexDump(m_data);
        std.debug.print("\nactual output:\n", .{});
        hexDump(actual_m_data);
        return results.fail(mml_path, "output file data does not match", .{});
    }
    if (!std.mem.eql(u8, console_data, process_output.stdout)) {
        std.debug.print("expected output:\n", .{});
        hexDump(console_data);
        std.debug.print("\nactual output:\n", .{});
        hexDump(process_output.stdout);
        return results.fail(mml_path, "console output does not match", .{});
    }
    results.pass(mml_path);
}

fn hexDump(data: []const u8) void {
    std.debug.print("     0011 2233 4455 6677 8899 AABB CCDD EEFF\n", .{});
    var addr: usize = 0;
    var rows = std.mem.window(u8, data, 16, 16);
    while (rows.next()) |row| : (addr += 16) {
        std.debug.print("{X:0>4}", .{addr});
        var chunks = std.mem.window(u8, row, 2, 2);
        while (chunks.next()) |chunk| {
            switch (chunk.len) {
                0 => std.debug.print("     ", .{}),
                1 => std.debug.print(" {X:0>2}  ", .{chunk[0]}),
                2 => std.debug.print(" {X:0>2}{X:0>2}", .{ chunk[0], chunk[1] }),
                else => unreachable,
            }
        }
        for (0..(16 - row.len) / 2) |_| std.debug.print("     ", .{});
        std.debug.print(" ", .{});
        for (row) |byte| {
            std.debug.print("{c}", .{if (std.ascii.isPrint(byte)) byte else '.'});
        }
        std.debug.print("\n", .{});
    }
}

const Results = struct {
    n_pass: u32,
    n_fail: u32,

    const init: Results = .{ .n_pass = 0, .n_fail = 0 };

    fn pass(results: *Results, mml_path: []const u8) void {
        log.info("pass: {s}", .{mml_path});
        results.n_pass += 1;
    }

    fn fail(results: *Results, mml_path: []const u8, comptime fmt: []const u8, args: anytype) void {
        log.err("fail: {s}: " ++ fmt, .{mml_path} ++ args);
        results.n_fail += 1;
    }
};
