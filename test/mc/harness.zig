pub fn main(init: std.process.Init) !u8 {
    const gpa = init.gpa;
    const arena = init.arena.allocator();
    const io = init.io;

    var args = try init.minimal.args.toSlice(arena);
    if (args.len >= 2 and std.mem.eql(u8, args[1], "--verbose")) {
        args = args[2..];
        verbose = true;
    } else if (args.len >= 1) {
        args = args[1..];
    }
    if (args.len != 2) {
        log.err("usage: harness [--verbose] MC-PATH TEST-DIR", .{});
        return 1;
    }
    const mc_path = args[0];
    const test_dir_path = args[1];

    const tmp_dir = try Io.Dir.cwd().createDirPathOpen(io, ".zig-cache/tmp", .{});
    defer tmp_dir.close(io);

    // runTest will be using a different working directory for each test, so
    // it is easiest to use an absolute path for MC.
    const mc_path_abs = try Io.Dir.cwd().realPathFileAlloc(io, mc_path, arena);

    var results: Results = .init;
    const test_dir = try Io.Dir.cwd().openDir(io, test_dir_path, .{ .iterate = true });
    defer test_dir.close(io);
    var test_dir_iterator = test_dir.iterate();
    while (try test_dir_iterator.next(io)) |test_dir_entry| {
        if (std.mem.endsWith(u8, test_dir_entry.name, ".MML")) {
            try runTest(gpa, io, test_dir_entry.name, test_dir, tmp_dir, mc_path_abs, &results);
        }
    }
    log.info("pass: {}, fail: {}", .{ results.n_pass, results.n_fail });
    return if (results.n_fail == 0) 0 else 1;
}

fn runTest(
    gpa: Allocator,
    io: Io,
    mml_path: []const u8,
    test_dir: Io.Dir,
    tmp_dir: Io.Dir,
    mc_path: []const u8,
    results: *Results,
) !void {
    const Base64Encoder = std.base64.url_safe.Encoder;
    var random_bytes: [12]u8 = undefined;
    io.random(&random_bytes);
    var run_dir_name_buf: [Base64Encoder.calcSize(random_bytes.len)]u8 = undefined;
    const run_dir_name = Base64Encoder.encode(&run_dir_name_buf, &random_bytes);
    const run_dir = try tmp_dir.createDirPathOpen(io, run_dir_name, .{});
    defer {
        run_dir.close(io);
        tmp_dir.deleteTree(io, run_dir_name) catch {};
    }

    const data_limit: Io.Limit = .limited(std.math.maxInt(u16));

    const mml = try test_dir.readFileAlloc(io, mml_path, gpa, data_limit);
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

    try run_dir.writeFile(io, .{
        .sub_path = mml_path,
        .data = mml,
    });

    const m_path = m_path_option orelse
        return results.fail(mml_path, "missing output file comment", .{});
    const console_path = console_path_option orelse
        return results.fail(mml_path, "missing console output file comment", .{});

    const m_data = try test_dir.readFileAlloc(io, m_path, gpa, data_limit);
    defer gpa.free(m_data);
    const console_data = try test_dir.readFileAlloc(io, console_path, gpa, data_limit);
    defer gpa.free(console_data);

    var env: std.process.Environ.Map = .init(gpa);
    defer env.deinit();
    const process_output = try std.process.run(gpa, io, .{
        .argv = &.{ mc_path, mml_path },
        .cwd = .{ .dir = run_dir },
        .environ_map = &env,
        .stdout_limit = .limited(4096),
        .stderr_limit = .limited(4096),
    });
    defer gpa.free(process_output.stdout);
    defer gpa.free(process_output.stderr);

    switch (process_output.term) {
        .exited => |status| if (status != 0) return results.fail(mml_path, "compiler exited with status {}", .{status}),
        else => |term| return results.fail(mml_path, "compiler exited abnormally: {}", .{term}),
    }

    const actual_m_data = run_dir.readFileAlloc(io, m_path, gpa, data_limit) catch |err| switch (err) {
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

var verbose = false;

fn logFn(
    comptime level: log.Level,
    comptime scope: @EnumLiteral(),
    comptime format: []const u8,
    args: anytype,
) void {
    if (!verbose and level != .err) return;
    log.defaultLog(level, scope, format, args);
}

pub const std_options: std.Options = .{
    .logFn = logFn,
};

const std = @import("std");
const Allocator = std.mem.Allocator;
const Io = std.Io;
const log = std.log;
