// Runs the trace host on every .RUN case and compares its output with the
// goldens captured by repro.py.

pub fn main(init: std.process.Init) !u8 {
    const gpa = init.gpa;
    const arena = init.arena.allocator();
    const io = init.io;

    var args = try init.minimal.args.toSlice(arena);
    if (args.len >= 1) args = args[1..];
    var replay = false;
    while (args.len > 0 and std.mem.startsWith(u8, args[0], "--")) {
        if (std.mem.eql(u8, args[0], "--verbose")) {
            verbose = true;
        } else if (std.mem.eql(u8, args[0], "--replay")) {
            replay = true;
        } else {
            log.err("unknown option {s}", .{args[0]});
            return 1;
        }
        args = args[1..];
    }
    if (args.len != 2) {
        log.err("usage: harness [--verbose] [--replay] TRACE-PATH TEST-DIR", .{});
        return 1;
    }
    const trace_path = args[0];
    const test_dir_path = args[1];

    const tmp_dir = try Io.Dir.cwd().createDirPathOpen(io, ".zig-cache/tmp", .{});
    defer tmp_dir.close(io);

    // runTest uses a different working directory for each case, so the trace
    // binary and the cases directory need absolute paths.
    const trace_path_abs = try Io.Dir.cwd().realPathFileAlloc(io, trace_path, arena);
    const test_dir_abs = try Io.Dir.cwd().realPathFileAlloc(io, test_dir_path, arena);

    var results: Results = .init;
    const test_dir = try Io.Dir.cwd().openDir(io, test_dir_path, .{ .iterate = true });
    defer test_dir.close(io);
    var test_dir_iterator = test_dir.iterate();
    while (try test_dir_iterator.next(io)) |test_dir_entry| {
        if (std.mem.endsWith(u8, test_dir_entry.name, ".RUN")) {
            try runTest(gpa, io, test_dir_entry.name, test_dir, tmp_dir, trace_path_abs, test_dir_abs, replay, &results);
        }
    }
    log.info("pass: {}, fail: {}", .{ results.n_pass, results.n_fail });
    return if (results.n_fail == 0) 0 else 1;
}

fn runTest(
    gpa: Allocator,
    io: Io,
    run_path: []const u8,
    test_dir: Io.Dir,
    tmp_dir: Io.Dir,
    trace_path: []const u8,
    test_dir_path: []const u8,
    replay: bool,
    results: *Results,
) !void {
    const name = run_path[0 .. run_path.len - ".RUN".len];

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

    var env: std.process.Environ.Map = .init(gpa);
    defer env.deinit();
    var argv_buf: [4][]const u8 = undefined;
    const argv: []const []const u8 = if (replay) blk: {
        argv_buf[0..4].* = .{ trace_path, "--replay", test_dir_path, name };
        break :blk argv_buf[0..4];
    } else blk: {
        argv_buf[0..3].* = .{ trace_path, test_dir_path, name };
        break :blk argv_buf[0..3];
    };
    const process_output = try std.process.run(gpa, io, .{
        .argv = argv,
        .cwd = .{ .dir = run_dir },
        .environ_map = &env,
        .stdout_limit = .limited(4096),
        .stderr_limit = .limited(4096),
    });
    defer gpa.free(process_output.stdout);
    defer gpa.free(process_output.stderr);

    switch (process_output.term) {
        .exited => |status| switch (status) {
            0 => {},
            else => return results.fail(run_path, "trace host exited with status {}", .{status}),
        },
        else => |term| return results.fail(run_path, "trace host exited abnormally: {}", .{term}),
    }

    var log_buf: [256]u8 = undefined;
    var out_buf: [256]u8 = undefined;
    const log_name = try std.fmt.bufPrint(&log_buf, "{s}.LOG", .{name});
    const out_name = try std.fmt.bufPrint(&out_buf, "{s}.OUT", .{name});
    if (!try compareFile(gpa, io, test_dir, run_dir, log_name, compareLog, run_path, results)) return;
    if (!try compareFile(gpa, io, test_dir, run_dir, out_name, compareText, run_path, results)) return;
    results.pass(run_path);
}

// Returns false once a difference has been reported.
fn compareFile(
    gpa: Allocator,
    io: Io,
    test_dir: Io.Dir,
    run_dir: Io.Dir,
    sub_path: []const u8,
    comptime compare: fn ([]const u8, []const u8) void,
    run_path: []const u8,
    results: *Results,
) !bool {
    const data_limit: Io.Limit = .limited(std.math.maxInt(u16));
    const expected = test_dir.readFileAlloc(io, sub_path, gpa, data_limit) catch |err| switch (err) {
        error.FileNotFound => {
            results.fail(run_path, "golden {s} is missing", .{sub_path});
            return false;
        },
        else => |other| return other,
    };
    defer gpa.free(expected);
    const actual = run_dir.readFileAlloc(io, sub_path, gpa, data_limit) catch |err| switch (err) {
        error.FileNotFound => {
            results.fail(run_path, "trace host did not produce {s}", .{sub_path});
            return false;
        },
        else => |other| return other,
    };
    defer gpa.free(actual);

    if (!std.mem.eql(u8, expected, actual)) {
        std.debug.print("{s}: {s} differs\n", .{ run_path, sub_path });
        compare(expected, actual);
        results.fail(run_path, "{s} does not match the golden", .{sub_path});
        return false;
    }
    return true;
}

fn compareLog(expected: []const u8, actual: []const u8) void {
    var expected_lines = std.mem.splitScalar(u8, expected, '\n');
    var actual_lines = std.mem.splitScalar(u8, actual, '\n');
    var expected_history: [3][]const u8 = undefined;
    var actual_history: [3][]const u8 = undefined;
    var index: usize = 0;
    var history_len: usize = 0;
    while (true) : (index += 1) {
        const expected_line = expected_lines.next();
        const actual_line = actual_lines.next();
        if (expected_line == null and actual_line == null) return;
        if (expected_line != null and actual_line != null and
            std.mem.eql(u8, expected_line.?, actual_line.?))
        {
            expected_history[history_len % 3] = expected_line.?;
            actual_history[history_len % 3] = actual_line.?;
            history_len += 1;
            continue;
        }
        const clock = if (expected_line) |line| firstWord(line) else "?";
        std.debug.print("  first difference at trace line {} (clock {s})\n", .{ index + 1, clock });
        printHistory(expected_history, history_len, "expected");
        printHistory(actual_history, history_len, "actual  ");
        std.debug.print("  expected: {s}\n", .{expected_line orelse "<end of file>"});
        std.debug.print("  actual:   {s}\n", .{actual_line orelse "<end of file>"});
        var context: usize = 0;
        while (context < 3) : (context += 1) {
            const e = expected_lines.next();
            const a = actual_lines.next();
            if (e == null and a == null) break;
            std.debug.print("  expected+: {s}\n", .{e orelse "<end of file>"});
            std.debug.print("  actual+:   {s}\n", .{a orelse "<end of file>"});
        }
        return;
    }
}

fn firstWord(line: []const u8) []const u8 {
    var end: usize = 0;
    while (end < line.len and line[end] != ' ') end += 1;
    return line[0..end];
}

fn printHistory(history: [3][]const u8, len: usize, label: []const u8) void {
    const count = @min(len, 3);
    var i: usize = 0;
    while (i < count) : (i += 1) {
        const slot = (len - count + i) % 3;
        std.debug.print("  {s}-: {s}\n", .{ label, history[slot] });
    }
}

// .OUT is UTF-8 text; report the first differing line like the trace, but
// compare the bytes exactly the way test/mc compares console output.
fn compareText(expected: []const u8, actual: []const u8) void {
    var index: usize = 0;
    while (index < expected.len and index < actual.len and expected[index] == actual[index]) index += 1;
    const line = 1 + std.mem.count(u8, expected[0..index], "\n");
    std.debug.print("  first difference at byte {} (line {})\n", .{ index, line });
    printTextContext(expected, actual, index);
}

fn printTextContext(expected: []const u8, actual: []const u8, at: usize) void {
    const expected_line = lineContaining(expected, at);
    const actual_line = lineContaining(actual, at);
    std.debug.print("  expected: {s}\n", .{expected_line});
    std.debug.print("  actual:   {s}\n", .{actual_line});
}

fn lineContaining(text: []const u8, at: usize) []const u8 {
    const pos = @min(at, text.len);
    const start = (std.mem.lastIndexOfScalar(u8, text[0..pos], '\n') orelse 0) +
        @intFromBool(std.mem.lastIndexOfScalar(u8, text[0..pos], '\n') != null);
    const end = std.mem.indexOfScalarPos(u8, text, pos, '\n') orelse text.len;
    return text[start..end];
}

const Results = struct {
    n_pass: u32,
    n_fail: u32,

    const init: Results = .{ .n_pass = 0, .n_fail = 0 };

    fn pass(results: *Results, run_path: []const u8) void {
        log.info("pass: {s}", .{run_path});
        results.n_pass += 1;
    }

    fn fail(results: *Results, run_path: []const u8, comptime fmt: []const u8, args: anytype) void {
        log.err("fail: {s}: " ++ fmt, .{run_path} ++ args);
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
