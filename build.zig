
const std = @import("std");
const builtin = @import("builtin");
const assert = std.debug.assert;
const Md5 = std.crypto.hash.Md5;
const Io = std.Io;

/// Version strings derived once from git.
const Versions = struct {
    full: []const u8,
    major: []const u8,
    minor: []const u8,
    patch: []const u8,
    date: []const u8,
    commit_url: []const u8,
};

/// AMXX/Metamod glue every module compiles against its own moduleconfig.h.
const shared_glue_sources = &[_][]const u8{
    "amxxmodule.cpp",
    "mod_rehlds_api.cpp",
};

/// One AMXX module to build.
const ModuleOptions = struct {
    /// Base artifact name; becomes `<name>_amxx` (Windows) / `<name>_amxx_i386` (Linux).
    name: []const u8,
    /// Module source dir, e.g. "src/kz_global_api"; its `include/` is added and its tree hashed.
    dir: []const u8,
    /// Module source files, relative to `dir`.
    sources: []const []const u8,
};

/// Everything shared across AMXX modules. Populate once, then call `addModule` per module.
const ModuleContext = struct {
    b: *std.Build,
    target: std.Build.ResolvedTarget,
    optimise: std.builtin.OptimizeMode,
    versions: Versions,
    cflags: []const []const u8,
    metamod: *std.Build.Dependency,
    hlsdk: *std.Build.Dependency,
    memtools: *std.Build.Step.Compile,

    fn addModule(ctx: ModuleContext, io: Io, opts: ModuleOptions) !*std.Build.Step.Compile {
        const b = ctx.b;
        const is_linux = ctx.target.result.os.tag == .linux;

        // AMXX expects <name>_amxx_i386.so (Linux) / <name>_amxx.dll (Windows) -- no "lib" prefix.
        const art_name = if (is_linux)
            b.fmt("{s}_amxx_i386", .{opts.name})
        else
            b.fmt("{s}_amxx", .{opts.name});

        const lib = b.addLibrary(.{
            .name = art_name,
            .linkage = .dynamic,
            .root_module = b.createModule(.{
                .target = ctx.target,
                .optimize = ctx.optimise,
                .link_libc = true,
            }),
        });

        lib.root_module.addCMacro("JIT", "");
        lib.root_module.addCMacro("ASM32", "");
        lib.root_module.addCMacro("HAVE_STDINT_H", "");
        lib.root_module.addCMacro("g_rehlds_available", "RehldsApi");

        try ctx.addVersionMacros(lib, opts);
        ctx.addPlatformConfig(io, lib);
        ctx.addSdkIncludePaths(lib, opts.dir);

        lib.root_module.linkLibrary(ctx.memtools);
        lib.root_module.addCSourceFiles(.{ .root = b.path("src/shared"), .files = shared_glue_sources, .flags = ctx.cflags });
        lib.root_module.addCSourceFiles(.{ .root = b.path(opts.dir), .files = opts.sources, .flags = ctx.cflags });

        // Install under the AMXX filename directly (Zig would otherwise prefix "lib" on the .so).
        const install = b.addInstallArtifact(lib, .{
            .dest_sub_path = b.fmt("{s}{s}", .{ art_name, if (is_linux) ".so" else ".dll" }),
        });
        b.getInstallStep().dependOn(&install.step);

        return lib;
    }

    fn addVersionMacros(ctx: ModuleContext, lib: *std.Build.Step.Compile, opts: ModuleOptions) !void {
        const b = ctx.b;
        const v = ctx.versions;

        const checksum = try hashFilesInDir(b.graph.io, b.allocator, opts.dir);
        lib.root_module.addCMacro("MODULE_CHECKSUM", b.fmt("\"{s}\"", .{checksum}));
        lib.root_module.addCMacro("MODULE_VERSION_MAJOR", v.major);
        lib.root_module.addCMacro("MODULE_VERSION_MINOR", v.minor);
        lib.root_module.addCMacro("MODULE_VERSION_PATCH", v.patch);
        lib.root_module.addCMacro("MODULE_VERSION", b.fmt("\"{s}\"", .{v.full}));
        lib.root_module.addCMacro("MODULE_COMMIT_URL", b.fmt("\"{s}\"", .{v.commit_url}));
        lib.root_module.addCMacro("MODULE_DATE", b.fmt("\"{s}\"", .{v.date}));
        std.debug.print("  {s:<14} {s:<26} md5 {s}\n", .{ opts.name, v.full, checksum });
    }

    fn addPlatformConfig(ctx: ModuleContext, io: Io, lib: *std.Build.Step.Compile) void {
        switch (ctx.target.result.os.tag) {
            .windows => {
                if (comptime builtin.target.os.tag != .windows) addWindowsHShim(ctx.b, lib);
                lib.root_module.addCMacro("WIN32", "");
                lib.root_module.addCMacro("_WINDOWS", "");
                lib.root_module.addCMacro("CBASE_DLLEXPORT", "__declspec(dllexport)");
                lib.root_module.linkSystemLibrary("ws2_32", .{});
            },
            .linux => {
                if (std.Io.Dir.accessAbsolute(io, "/usr/lib32/", .{})) {
                    lib.root_module.addLibraryPath(.{ .cwd_relative = "/usr/lib32/" });
                } else |_| {}
                if (std.Io.Dir.accessAbsolute(io, "/usr/lib/i386-linux-gnu/", .{})) {
                    lib.root_module.addLibraryPath(.{ .cwd_relative = "/usr/lib/i386-linux-gnu/" });
                } else |_| {}
                lib.root_module.addSystemIncludePath(.{ .cwd_relative = "/usr/include/" });
                lib.root_module.addCMacro("linux", "");
                lib.root_module.addCMacro("LINUX", "");
                lib.root_module.addCMacro("POSIX", "");
                lib.root_module.addCMacro("_LINUX", "");
                lib.root_module.linkSystemLibrary("pthread", .{});
                lib.root_module.linkSystemLibrary("dl", .{});
            },
            else => unreachable,
        }
    }

    fn addSdkIncludePaths(ctx: ModuleContext, lib: *std.Build.Step.Compile, dir: []const u8) void {
        const b = ctx.b;
        lib.root_module.addIncludePath(b.path("deps/sdk/amxmodx/public/resdk"));
        lib.root_module.addIncludePath(b.path("deps/sdk/amxmodx/public"));
        lib.root_module.addIncludePath(b.path("src/shared"));
        lib.root_module.addIncludePath(b.path(b.fmt("{s}/include", .{dir})));
        lib.root_module.addIncludePath(ctx.metamod.path("metamod"));
        lib.root_module.addIncludePath(ctx.hlsdk.path(""));
        lib.root_module.addIncludePath(ctx.hlsdk.path("common"));
        lib.root_module.addIncludePath(ctx.hlsdk.path("dlls"));
        lib.root_module.addIncludePath(ctx.hlsdk.path("engine"));
        lib.root_module.addIncludePath(ctx.hlsdk.path("game_shared"));
        lib.root_module.addIncludePath(ctx.hlsdk.path("public"));
        lib.root_module.addIncludePath(ctx.hlsdk.path("pm_shared"));
        lib.root_module.link_libcpp = true;
    }
};

pub fn build(b: *std.Build) !void
{
    const io = b.graph.io;
    try buildHostUnitTests(b);

    const test_only = b.option(bool, "test-only", "Build and run host unit tests only (skip AMXX module)") orelse false;
    if (test_only) return;

    const target = b.standardTargetOptions(.{});

	if (target.result.os.tag != .linux and target.result.os.tag != .windows)
	{
		std.debug.print("{} is not supported as a build target.\n", .{target.result.os.tag});
		return;
	}

	if (target.result.cpu.arch != .x86)
	{
		std.debug.print("Only x86 is supported as a build target.\n", .{});
		return;
	}

	const optimise = b.standardOptimizeOption(.{});

	// parson
	const dep_parson = b.dependency("parson", .{
		.target = target,
		.optimize = .ReleaseFast,
	});

	const parson = b.addLibrary(.{
		.name = "parson",
		.root_module = b.createModule(.{
			.target = target,
			.optimize = .ReleaseFast,
			.link_libc = true,
		}),
	});

	parson.root_module.addCSourceFile(.{
		.file = dep_parson.path("parson.c"),
		.flags = &.{"-std=c89"},
	});

	parson.root_module.addIncludePath(dep_parson.path(""));
	parson.root_module.link_libc = true;

	parson.installHeadersDirectory(dep_parson.path(""), "parson", .{});

	// sqlitecpp (bundles sqlite3/sqlite3.c, so no separate system sqlite3 link is needed)
	const dep_sqlitecpp = b.dependency("sqlitecpp", .{
		.target = target,
		.optimize = .ReleaseFast,
	});

	const sqlitecpp = b.addLibrary(.{
		.name = "sqlitecpp",
		.root_module = b.createModule(.{
			.target = target,
			.optimize = .ReleaseFast,
			.link_libc = true,
		}),
	});

	sqlitecpp.root_module.addIncludePath(dep_sqlitecpp.path("include"));
	sqlitecpp.root_module.addIncludePath(dep_sqlitecpp.path("sqlite3"));

	sqlitecpp.root_module.addCSourceFiles(.{
		.root = dep_sqlitecpp.path(""),
		.files = &.{
			"src/Backup.cpp",
			"src/Column.cpp",
			"src/Database.cpp",
			"src/Exception.cpp",
			"src/Savepoint.cpp",
			"src/Statement.cpp",
			"src/Transaction.cpp",
			"sqlite3/sqlite3.c",
		}
	});

	sqlitecpp.root_module.addCMacro("SQLITE_ENABLE_COLUMN_METADATA", "");
	sqlitecpp.root_module.link_libc = true;
	sqlitecpp.root_module.link_libcpp = true;

	// mbedtls
	const dep_mbedtls = b.dependency("mbedtls", .{
		.target = target,
		.optimize = .ReleaseFast,
	});

	const dep_mbedtls_c = dep_mbedtls.builder.dependency("mbedtls", .{});

	// zstd
	const dep_zstd = b.dependency("zstd", .{
		.target = target,
		.optimize = .ReleaseFast,
	});

	// ixwebsocket (USE_TLS via mbedtls)
	const dep_ixwebsocket = b.dependency("ixwebsocket", .{
		.target = target,
		.optimize = .ReleaseFast,
	});

	const ixwebsocket = b.addLibrary(.{
		.name = "ixwebsocket",
		.root_module = b.createModule(.{
			.target = target,
			.optimize = .ReleaseFast,
			.link_libc = true,
		}),
	});

	ixwebsocket.root_module.addCSourceFiles(.{
		.root = dep_ixwebsocket.path(""),
		.files = &.{
			"ixwebsocket/IXBench.cpp",
			"ixwebsocket/IXCancellationRequest.cpp",
			"ixwebsocket/IXConnectionState.cpp",
			"ixwebsocket/IXDNSLookup.cpp",
			"ixwebsocket/IXExponentialBackoff.cpp",
			"ixwebsocket/IXGetFreePort.cpp",
			"ixwebsocket/IXGzipCodec.cpp",
			"ixwebsocket/IXHttp.cpp",
			"ixwebsocket/IXHttpClient.cpp",
			"ixwebsocket/IXHttpServer.cpp",
			"ixwebsocket/IXNetSystem.cpp",
			"ixwebsocket/IXSelectInterrupt.cpp",
			"ixwebsocket/IXSelectInterruptFactory.cpp",
			"ixwebsocket/IXSelectInterruptPipe.cpp",
			"ixwebsocket/IXSelectInterruptEvent.cpp",
			"ixwebsocket/IXSetThreadName.cpp",
			"ixwebsocket/IXSocket.cpp",
			"ixwebsocket/IXSocketConnect.cpp",
			"ixwebsocket/IXSocketFactory.cpp",
			"ixwebsocket/IXSocketServer.cpp",
			"ixwebsocket/IXSocketTLSOptions.cpp",
			"ixwebsocket/IXStrCaseCompare.cpp",
			"ixwebsocket/IXUdpSocket.cpp",
			"ixwebsocket/IXUrlParser.cpp",
			"ixwebsocket/IXUuid.cpp",
			"ixwebsocket/IXUserAgent.cpp",
			"ixwebsocket/IXWebSocket.cpp",
			"ixwebsocket/IXWebSocketCloseConstants.cpp",
			"ixwebsocket/IXWebSocketHandshake.cpp",
			"ixwebsocket/IXWebSocketHttpHeaders.cpp",
			"ixwebsocket/IXWebSocketPerMessageDeflate.cpp",
			"ixwebsocket/IXWebSocketPerMessageDeflateCodec.cpp",
			"ixwebsocket/IXWebSocketPerMessageDeflateOptions.cpp",
			"ixwebsocket/IXWebSocketProxyServer.cpp",
			"ixwebsocket/IXWebSocketServer.cpp",
			"ixwebsocket/IXWebSocketTransport.cpp",
			"ixwebsocket/IXSocketMbedTLS.cpp",
		}
	});

	var mbedtlsVersionGreaterThan3: bool = true;

	dep_mbedtls_c.path("").getPath3(b, null).access(io, "include/mbedtls/build_info.h",  .{}) catch {
		mbedtlsVersionGreaterThan3 = false;
	};

	ixwebsocket.root_module.addCMacro("IXWEBSOCKET_USE_TLS", "");
	ixwebsocket.root_module.addCMacro("IXWEBSOCKET_USE_MBED_TLS", "");
	if (mbedtlsVersionGreaterThan3)
	{
		ixwebsocket.root_module.addCMacro("IXWEBSOCKET_USE_MBED_TLS_MIN_VERSION_3", "");
	}

	ixwebsocket.root_module.addIncludePath(dep_ixwebsocket.path(""));
	ixwebsocket.root_module.addIncludePath(dep_ixwebsocket.path("ixwebsocket/"));
	ixwebsocket.root_module.addIncludePath(dep_mbedtls.path(""));
	if (target.result.os.tag == .linux)
	{
		ixwebsocket.root_module.addSystemIncludePath(.{.cwd_relative = "/usr/include/"});
	}
	ixwebsocket.root_module.link_libc = true;
	ixwebsocket.root_module.link_libcpp = true;
	ixwebsocket.root_module.linkLibrary(dep_mbedtls.artifact("mbedtls"));

	// SPSCQueue
	const dep_spscqueue = b.dependency("spscqueue", .{});

	// metamod
	const dep_metamod = b.dependency("metamod", .{});

	// hlsdk
	const dep_hlsdk = b.dependency("hlsdk", .{});

	// memtools (amxx) -- shared by every module
	const memtools = b.addLibrary(.{
		.name = "memtools",
		.root_module = b.createModule(.{
			.target = target,
			.optimize = .ReleaseFast,
			.link_libc = true,
		}),
	});

	memtools.root_module.addIncludePath(b.path("deps/sdk/amxmodx/public"));
	if (target.result.os.tag == .windows)
	{
		memtools.root_module.addCMacro("WIN32", "");
	}
	memtools.root_module.addCSourceFiles(.{
		.files = &.{
			"deps/sdk/amxmodx/public/memtools/MemoryUtils.cpp",
			"deps/sdk/amxmodx/public/memtools/CDetour/detours.cpp",
		},
		.flags = &.{"-std=c++11", "-Wno-register"}
	});
	memtools.root_module.addCSourceFiles(.{.files = &.{"deps/sdk/amxmodx/public/memtools/CDetour/asm/asm.c"}});

	memtools.root_module.link_libc = true;
	memtools.root_module.link_libcpp = true;

	const ctx = ModuleContext{
		.b = b,
		.target = target,
		.optimise = optimise,
		.versions = computeVersions(b),
		.cflags = try amxxCflags(b, target, optimise),
		.metamod = dep_metamod,
		.hlsdk = dep_hlsdk,
		.memtools = memtools,
	};

	// kz_global_api -- main module: WebSocket client, SQLite storage, replay codecs, ReHLDS hooks.
	const kz_global_api = try ctx.addModule(io, .{
		.name = "kz_global_api",
		.dir = "src/kz_global_api",
		.sources = &.{
			"krp_format.cpp",
			"krp_header_validate.cpp",
			"kz_basic_ac.cpp",
			"kz_cvars.cpp",
			"kz_natives.cpp",
			"kz_path_validate.cpp",
			"kz_replay_uid.cpp",
			"kz_replay.cpp",
			"kz_replay_pb.cpp",
			"kz_storage.cpp",
			"kz_util.cpp",
			"kz_ws.cpp",
			"kz_ws_msgs.cpp",
			"main.cpp",
		},
	});

	kz_global_api.root_module.linkLibrary(parson);
	kz_global_api.root_module.linkLibrary(sqlitecpp);
	kz_global_api.root_module.linkLibrary(ixwebsocket);
	kz_global_api.root_module.linkLibrary(dep_zstd.artifact("zstd"));

	kz_global_api.root_module.addIncludePath(dep_zstd.path("lib"));
	kz_global_api.root_module.addIncludePath(dep_ixwebsocket.path(""));
	kz_global_api.root_module.addIncludePath(dep_sqlitecpp.path("include/"));
	kz_global_api.root_module.addIncludePath(dep_parson.path(""));
	kz_global_api.root_module.addIncludePath(dep_spscqueue.path("include"));

	if (target.result.os.tag == .windows)
	{
		kz_global_api.root_module.linkSystemLibrary("mswsock", .{});
		kz_global_api.root_module.linkSystemLibrary("crypt32", .{});
	}
	else if (target.result.os.tag == .linux)
	{
		kz_global_api.root_module.linkSystemLibrary("z", .{});
	}

	// kz_base -- second module (2nd binary). Shares the glue/SDK headers; links no heavy deps yet.
	const kz_base = try ctx.addModule(io, .{
		.name = "kz_base",
		.dir = "src/kz_base",
		.sources = &.{
                    "kz_api.cpp",
                    "kz_cmd.cpp",
                    "kz_base.cpp",
                    "kz_util.cpp",
                    "kz_mpbhop.cpp",
                    "main.cpp"
                }
	});

	var cdb_targets: std.ArrayList(*std.Build.Step.Compile) = .empty;
	try cdb_targets.append(b.allocator, kz_global_api);
	try cdb_targets.append(b.allocator, kz_base);
	const zcc = @import("compile_commands");
	_ = zcc.createStep(b, "cdb", try cdb_targets.toOwnedSlice(b.allocator));
}

fn hashFilesInDir(io: Io, allocator: std.mem.Allocator, dir_path: []const u8) ![]const u8 {
    var md5 = std.crypto.hash.Md5.init(.{});

    var src_dir = try std.Io.Dir.cwd().openDir(io, dir_path, .{ .iterate = true });
    defer src_dir.close(io);

    var walker = try src_dir.walk(allocator);
    defer walker.deinit();

    var file_paths: std.ArrayList([]const u8) = .empty;
    defer {
        for (file_paths.items) |p| allocator.free(p);
        file_paths.deinit(allocator);
    }

    while (try walker.next(io)) |entry| {
        if (entry.kind == .file) {
            const path_copy = try allocator.dupe(u8, entry.path);
            try file_paths.append(allocator, path_copy);
        }
    }

    std.mem.sort([]const u8, file_paths.items, {}, struct {
        fn less(_: void, a: []const u8, b: []const u8) bool {
            return std.mem.lessThan(u8, a, b);
        }
    }.less);

    for (file_paths.items) |file_path| {
        const file = try src_dir.openFile(io, file_path, .{});
        defer file.close(io);

        md5.update(file_path);

        var buffer: [4096]u8 = undefined;
        while (true) {
            const bytes_read = file.readStreaming(io, &.{&buffer}) catch 0;
            if (bytes_read == 0) break;
            md5.update(buffer[0..bytes_read]);
        }
    }

    var digest: [std.crypto.hash.Md5.digest_length]u8 = undefined;
    md5.final(&digest);

    const hex_chars = std.fmt.bytesToHex(digest, .lower);
    return allocator.dupe(u8, &hex_chars);
}

fn getBuildDate(b: *std.Build) []const u8 {
    const res = std.process.run(b.allocator, b.graph.io, .{
        .argv = &.{ "git", "log", "-1", "--format=%cs", "--date=short" },
    }) catch return "unknown";

    const date = std.mem.trimEnd(u8, res.stdout, "\r\n");
    if (date.len == 0) return "unknown";
    return b.fmt("{s}", .{date});
}

fn getGitCommit(b: *std.Build) []const u8 {
    const res = std.process.run(b.allocator, b.graph.io, .{
        .argv = &.{ "git", "rev-parse", "HEAD" },
    }) catch return "unknown";

    const commit = std.mem.trimEnd(u8, res.stdout, "\r\n");
    if (commit.len == 0) return "unknown";
    return b.fmt("{s}", .{commit});
}

/// Web URL of the HEAD commit, derived from the `origin` remote (SSH forms normalised to https).
/// Returns "unknown" if git or the remote is unavailable.
fn getCommitUrl(b: *std.Build) []const u8 {
    const commit = getGitCommit(b);
    if (std.mem.eql(u8, commit, "unknown")) return "unknown";

    const res = std.process.run(b.allocator, b.graph.io, .{
        .argv = &.{ "git", "config", "--get", "remote.origin.url" },
    }) catch return "unknown";

    var remote = std.mem.trimEnd(u8, res.stdout, "\r\n");
    if (remote.len == 0) return "unknown";
    if (std.mem.endsWith(u8, remote, ".git")) remote = remote[0 .. remote.len - 4];

    // Normalise to https:  git@host:owner/repo  and  ssh://git@host/owner/repo  ->  https://host/owner/repo
    const base = if (std.mem.startsWith(u8, remote, "git@")) blk: {
        const rest = remote["git@".len..];
        break :blk if (std.mem.indexOfScalar(u8, rest, ':')) |colon|
            b.fmt("https://{s}/{s}", .{ rest[0..colon], rest[colon + 1 ..] })
        else
            b.fmt("https://{s}", .{rest});
    } else if (std.mem.startsWith(u8, remote, "ssh://git@"))
        b.fmt("https://{s}", .{remote["ssh://git@".len..]})
    else
        remote;

    // Strip any "user:token@" credentials so they never end up baked into the binary.
    return b.fmt("{s}/commit/{s}", .{ stripUrlUserinfo(b, base), commit });
}

/// Remove a `userinfo@` component (e.g. "user:token@") from a `scheme://` URL's authority.
fn stripUrlUserinfo(b: *std.Build, url: []const u8) []const u8 {
    const scheme = std.mem.indexOf(u8, url, "://") orelse return url;
    const after = scheme + 3;
    const rest = url[after..];
    const authority_end = std.mem.indexOfScalar(u8, rest, '/') orelse rest.len;
    const at = std.mem.indexOfScalar(u8, rest[0..authority_end], '@') orelse return url;
    return b.fmt("{s}{s}", .{ url[0..after], rest[at + 1 ..] });
}

fn getGitVersion(b: *std.Build) []const u8 {
    const desc_res = std.process.run(b.allocator, b.graph.io, .{
        .argv = &.{ "git", "describe", "--tags", "--always", "--abbrev=7" },
    }) catch return "0.1.0-unknown";

    const status_res = std.process.run(b.allocator, b.graph.io, .{
        .argv = &.{ "git", "status", "--porcelain" },
    }) catch return "0.1.0-unknown";

    var tag_out = std.mem.trimEnd(u8, desc_res.stdout, "\r\n");
    if (tag_out.len > 0 and tag_out[0] == 'v') tag_out = tag_out[1..];

    const clean_status = std.mem.trimEnd(u8, status_res.stdout, "\r\n ");
    const is_dirty = clean_status.len > 0;

    if (!std.mem.containsAtLeast(u8, tag_out, 1, "-g")) {
        const hash_res = std.process.run(b.allocator, b.graph.io, .{
            .argv = &.{ "git", "rev-parse", "--short=7", "HEAD" },
        }) catch return "0.1.0-unknown";

        const hash_out = std.mem.trimEnd(u8, hash_res.stdout, "\r\n");

        return b.fmt("{s}-g{s}{s}", .{ tag_out, hash_out, if (is_dirty) "-dirty" else "" });
    }

    return b.fmt("{s}{s}", .{
        tag_out,
        if (is_dirty and !std.mem.endsWith(u8, tag_out, "-dirty")) "-dirty" else ""
    });
}

fn computeVersions(b: *std.Build) Versions {
    const full = getGitVersion(b);

    var tokenizer = std.mem.tokenizeScalar(u8, full, '.');
    const major = tokenizer.next() orelse "0";
    const minor = tokenizer.next() orelse "0";

    const raw_patch = tokenizer.next() orelse "0";
    var patch_tokenizer = std.mem.tokenizeScalar(u8, raw_patch, '-');
    const patch = patch_tokenizer.next() orelse "0";

    return .{ .full = full, .major = major, .minor = minor, .patch = patch, .date = getBuildDate(b), .commit_url = getCommitUrl(b) };
}

fn amxxCflags(b: *std.Build, target: std.Build.ResolvedTarget, optimise: std.builtin.OptimizeMode) ![]const []const u8 {
    var cflags: std.ArrayList([]const u8) = .empty;
    try cflags.appendSlice(b.allocator, &.{
        "-std=c++17",
        "-fno-strict-aliasing",           // HLSDK/pdata.h type-pun entity data through mismatched pointers; -O2+ strict aliasing miscompiles them
        "-fno-sanitize=pointer-overflow", // HLSDK's STRING() (pStringBase + offset) is pointer arithmetic that trips its pointer-overflow check
        "-Wall", "-Wextra", "-Werror",    // zig doesn't surface C warnings; make them hard errors
        "-Wno-unused-parameter",
        "-Wno-missing-field-initializers",
        "-Wno-deprecated-copy",
    });

    if (target.result.os.tag == .windows) {
        try cflags.appendSlice(b.allocator, &.{
            "-fsanitize-recover=undefined", // Windows-only: i386 Linux .so + libubsan_rt.a fails PIC linking
            "-Wno-macro-redefined",
            "-fpermissive",
        });
    }

    if (optimise == .ReleaseFast) {
        try cflags.appendSlice(b.allocator, &.{
            "-fomit-frame-pointer",        // free the frame-pointer register for general use
            "-funroll-loops",              // unroll hot loops to cut per-iteration branch overhead
            "-fno-rtti",                   // no typeid/dynamic_cast used: drop RTTI metadata
            "-fvisibility=hidden",         // hide symbols by default (only AMXX exports stay visible), smaller GOT/PLT
            "-fmerge-all-constants",       // fold identical constants to shrink the binary
        });
        if (target.result.os.tag != .windows) {
            try cflags.append(b.allocator,
                "-fno-semantic-interposition", // our own globals aren't interposed: allow inlining them in the .so
            );
        }
    }
    return cflags.toOwnedSlice(b.allocator);
}

/// Copy the toolchain's lowercase `windows.h` to `Windows.h` so amxmodx's `#include "Windows.h"`
/// resolves when cross-compiling from a case-sensitive host.
fn addWindowsHShim(b: *std.Build, compile: *std.Build.Step.Compile) void {
    const windows_h_path = b.pathJoin(&[_][]const u8{
        std.fs.path.dirname(b.graph.zig_exe) orelse unreachable,
        "lib/libc/include/any-windows-any/windows.h",
    });
    var write_file = b.addWriteFiles();
    _ = write_file.addCopyFile(.{ .cwd_relative = windows_h_path }, "Windows.h");
    compile.root_module.addIncludePath(write_file.getDirectory());
}

fn buildHostUnitTests(b: *std.Build) !void {
    const test_exe = b.addExecutable(.{
        .name = "kz_global_api_tests",
        .root_module = b.createModule(.{
            .target = b.graph.host,
            .optimize = .Debug,
        }),
    });
    test_exe.root_module.addIncludePath(b.path("src/kz_global_api/include"));
    test_exe.root_module.addCSourceFiles(.{
        .root = b.path("src/kz_global_api"),
        .files = &.{
            "kz_path_validate.cpp",
            "krp_header_validate.cpp",
            "kz_replay_uid.cpp",
            "test/test_main.cpp",
            "test/path_validate_test.cpp",
            "test/krp_validate_test.cpp",
            "test/replay_uid_test.cpp",
        },
        .flags = &.{"-std=c++17"},
    });
    test_exe.root_module.link_libcpp = true;
    const run_test = b.addRunArtifact(test_exe);
    const test_step = b.step("test", "Run host-side unit tests");
    test_step.dependOn(&run_test.step);
}
