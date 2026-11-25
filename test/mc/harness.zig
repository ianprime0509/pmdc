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
    if (args.len != 4) {
        log.err("usage: harness [--verbose] MC-PATH MCH-PATH EFC-PATH TEST-DIR", .{});
        return 1;
    }
    const test_dir_path = args[3];

    const tmp_dir = try Io.Dir.cwd().createDirPathOpen(io, ".zig-cache/tmp", .{});
    defer tmp_dir.close(io);

    // runTest will be using a different working directory for each test, so
    // it is easiest to use absolute paths for the commands.
    var runner: Runner = .{
        .gpa = gpa,
        .arena = arena,
        .io = io,
        .tmp_dir = tmp_dir,
        .exe_paths = .{
            .mc = try Io.Dir.cwd().realPathFileAlloc(io, args[0], arena),
            .mch = try Io.Dir.cwd().realPathFileAlloc(io, args[1], arena),
            .efc = try Io.Dir.cwd().realPathFileAlloc(io, args[2], arena),
        },
    };

    const test_dir = try Io.Dir.cwd().openDir(io, test_dir_path, .{ .iterate = true });
    defer test_dir.close(io);
    var test_dir_iterator = test_dir.iterate();
    while (try test_dir_iterator.next(io)) |test_dir_entry| {
        if (isCaseFile(test_dir_entry.name)) {
            try runner.runTest(test_dir, test_dir_entry.name, .mc);
        } else if (commandForDir(test_dir_entry.name)) |command| {
            try runner.runCommandDir(test_dir, test_dir_entry.name, command);
        }
    }
    log.info("pass: {}, fail: {}", .{ runner.results.n_pass, runner.results.n_fail });
    return if (runner.results.n_fail == 0) 0 else 1;
}

/// The command a case is compiled with, which is mc unless the case is in a
/// subdirectory named after another command.
const Command = enum {
    mc,
    mch,
    efc,
};

/// The extension a case's input file must have: mc and mch compile .MML, while
/// the effect compiler takes .EML.
fn caseExtension(command: Command) []const u8 {
    return switch (command) {
        .mc, .mch => ".MML",
        .efc => ".EML",
    };
}

/// Whether a file name looks like a case's input, whichever command it is for,
/// so that a case in the wrong place fails instead of being skipped.
fn isCaseFile(name: []const u8) bool {
    inline for (.{ ".MML", ".EML" }) |extension| {
        if (std.mem.endsWith(u8, name, extension)) return true;
    }
    return false;
}

fn commandForDir(name: []const u8) ?Command {
    inline for (.{ Command.mch, Command.efc }) |command| {
        if (std.mem.eql(u8, name, @tagName(command))) return command;
    }
    return null;
}

const Runner = struct {
    gpa: Allocator,
    arena: Allocator,
    io: Io,
    tmp_dir: Io.Dir,
    exe_paths: struct {
        mc: []const u8,
        mch: []const u8,
        efc: []const u8,
    },
    results: Results = .init,

    /// Runs the cases for a command other than mc, which are grouped in a
    /// subdirectory so that the mc cases stay in the top-level directory.
    fn runCommandDir(runner: *Runner, test_dir: Io.Dir, dir_name: []const u8, command: Command) !void {
        const io = runner.io;
        const command_dir = try test_dir.openDir(io, dir_name, .{ .iterate = true });
        defer command_dir.close(io);
        var command_dir_iterator = command_dir.iterate();
        while (try command_dir_iterator.next(io)) |entry| {
            if (isCaseFile(entry.name)) {
                try runner.runTest(command_dir, entry.name, command);
            }
        }
    }

    fn exePath(runner: *const Runner, command: Command) []const u8 {
        return switch (command) {
            .mc => runner.exe_paths.mc,
            .mch => runner.exe_paths.mch,
            .efc => runner.exe_paths.efc,
        };
    }

    fn runTest(runner: *Runner, test_dir: Io.Dir, mml_path: []const u8, command: Command) !void {
        const gpa = runner.gpa;
        const arena = runner.arena;
        const io = runner.io;
        const tmp_dir = runner.tmp_dir;
        const results = &runner.results;
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
        const headers = try parseHeaders(arena, mml);
        const display_path = if (command == .mc)
            mml_path
        else
            try std.fmt.allocPrint(arena, "{s}/{s}", .{ @tagName(command), mml_path });
        if (!checkCase(headers.command, command, mml_path, display_path, results)) return;

        try run_dir.writeFile(io, .{
            .sub_path = mml_path,
            .data = mml,
        });
        for (headers.extra_inputs) |input_path| {
            const data = test_dir.readFileAlloc(io, input_path, gpa, data_limit) catch |err| switch (err) {
                error.FileNotFound => return results.fail(display_path, "extra input file '{s}' not found", .{input_path}),
                else => |other_err| return other_err,
            };
            defer gpa.free(data);
            try run_dir.writeFile(io, .{
                .sub_path = std.fs.path.basename(input_path),
                .data = data,
            });
        }

        // mch only compiles and plays, so it cannot write a file to compare.
        if (headers.outputs.len == 0 and command != .mch) {
            return results.fail(display_path, "missing output file comment", .{});
        }
        const console_path = headers.console orelse
            return results.fail(display_path, "missing console output file comment", .{});

        const console_data = try test_dir.readFileAlloc(io, console_path, gpa, data_limit);
        defer gpa.free(console_data);

        var argv: std.ArrayList([]const u8) = .empty;
        try argv.append(arena, runner.exePath(command));
        var command_line_args = std.mem.tokenizeScalar(u8, headers.options orelse mml_path, ' ');
        while (command_line_args.next()) |arg| try argv.append(arena, arg);

        var env: std.process.Environ.Map = .init(gpa);
        defer env.deinit();
        const process_output = try std.process.run(gpa, io, .{
            .argv = argv.items,
            .cwd = .{ .dir = run_dir },
            .environ_map = &env,
            .stdout_limit = .limited(4096),
            .stderr_limit = .limited(4096),
        });
        defer gpa.free(process_output.stdout);
        defer gpa.free(process_output.stderr);

        switch (process_output.term) {
            .exited => |status| if (status != 0) return results.fail(display_path, "compiler exited with status {}", .{status}),
            else => |term| return results.fail(display_path, "compiler exited abnormally: {}", .{term}),
        }

        for (headers.outputs) |output_path| {
            const expected = test_dir.readFileAlloc(io, output_path, gpa, data_limit) catch |err| switch (err) {
                error.FileNotFound => return results.fail(display_path, "expected output file '{s}' has no golden", .{output_path}),
                else => |other_err| return other_err,
            };
            defer gpa.free(expected);
            const actual = run_dir.readFileAlloc(io, output_path, gpa, data_limit) catch |err| switch (err) {
                error.FileNotFound => return results.fail(display_path, "expected output file '{s}' not produced", .{output_path}),
                else => |other_err| return other_err,
            };
            defer gpa.free(actual);
            if (!std.mem.eql(u8, expected, actual)) {
                std.debug.print("expected {s}:\n", .{output_path});
                hexDump(expected);
                std.debug.print("\nactual {s}:\n", .{output_path});
                hexDump(actual);
                return results.fail(display_path, "output file '{s}' data does not match", .{output_path});
            }
        }
        if (!std.mem.eql(u8, console_data, process_output.stdout)) {
            std.debug.print("expected output:\n", .{});
            hexDump(console_data);
            std.debug.print("\nactual output:\n", .{});
            hexDump(process_output.stdout);
            return results.fail(display_path, "console output does not match", .{});
        }
        results.pass(display_path);
    }
};

/// The comment lines at the top of a test case's MML that tell the harness how
/// to run it.
const Headers = struct {
    /// Files the compiler is expected to produce, in `; Output:` order.
    outputs: []const []const u8 = &.{},
    /// The file the console output is compared against.
    console: ?[]const u8 = null,
    /// Files copied from the test directory into the run directory. They keep
    /// only their file name, so a test can keep a seed file whose name would
    /// otherwise collide with one of its goldens.
    extra_inputs: []const []const u8 = &.{},
    /// The command line passed to the compiler, minus the executable itself.
    options: ?[]const u8 = null,
    /// The command the case is for, from `; Command:`. Only cases for commands
    /// other than mc need it, and they must name the directory they are in.
    command: ?[]const u8 = null,
};

/// Checks that a case is in the directory for the command it names, with the
/// input extension that command takes. Returns false once the case has been
/// failed, since there is no point in running it.
fn checkCase(
    declared: ?[]const u8,
    command: Command,
    file_name: []const u8,
    display_path: []const u8,
    results: *Results,
) bool {
    const extension = caseExtension(command);
    if (!std.mem.endsWith(u8, file_name, extension)) {
        results.fail(display_path, "case files for {s} must end in {s}", .{
            @tagName(command),
            extension,
        });
        return false;
    }
    if (command == .mc) {
        if (declared) |name| {
            results.fail(display_path, "'; Command: {s}' is not allowed for mc cases", .{name});
            return false;
        }
        return true;
    }
    const name = declared orelse {
        results.fail(display_path, "missing '; Command: {s}' comment", .{@tagName(command)});
        return false;
    };
    if (!std.mem.eql(u8, name, @tagName(command))) {
        results.fail(display_path, "'; Command: {s}' does not match the {s} directory", .{
            name,
            @tagName(command),
        });
        return false;
    }
    return true;
}

fn parseHeaders(arena: Allocator, mml: []const u8) !Headers {
    const output_prefix = "; Output: ";
    const console_prefix = "; Console: ";
    const extra_input_prefix = "; Extra input: ";
    const options_prefix = "; Options: ";
    const command_prefix = "; Command: ";

    var headers: Headers = .{};
    var mml_lines = std.mem.splitSequence(u8, mml, "\r\n");
    while (mml_lines.next()) |line| {
        if (!std.mem.startsWith(u8, line, ";")) break;
        if (std.mem.startsWith(u8, line, output_prefix)) {
            headers.outputs = try splitList(arena, line[output_prefix.len..]);
        } else if (std.mem.startsWith(u8, line, console_prefix)) {
            headers.console = line[console_prefix.len..];
        } else if (std.mem.startsWith(u8, line, extra_input_prefix)) {
            headers.extra_inputs = try splitList(arena, line[extra_input_prefix.len..]);
        } else if (std.mem.startsWith(u8, line, options_prefix)) {
            headers.options = line[options_prefix.len..];
        } else if (std.mem.startsWith(u8, line, command_prefix)) {
            headers.command = line[command_prefix.len..];
        }
    }
    return headers;
}

fn splitList(arena: Allocator, text: []const u8) ![]const []const u8 {
    var items: std.ArrayList([]const u8) = .empty;
    var parts = std.mem.splitScalar(u8, text, ',');
    while (parts.next()) |part| {
        try items.append(arena, std.mem.trim(u8, part, " "));
    }
    return items.items;
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
