
const std = @import("std");
const builtin = @import("builtin");
const zcc = @import("compile_commands");
const assert = std.debug.assert;
const Md5 = std.crypto.hash.Md5;

fn hashFilesInSrc(allocator: std.mem.Allocator) ![]const u8 {
    var md5 = std.crypto.hash.Md5.init(.{});

    var src_dir = try std.fs.cwd().openDir("src", .{ .iterate = true });
    defer src_dir.close();

    var walker = try src_dir.walk(allocator);
    defer walker.deinit();


    var file_paths: std.ArrayList([]const u8) = .empty;
    defer {
        for (file_paths.items) |p| allocator.free(p);

        file_paths.deinit(allocator);
    }

    while (try walker.next()) |entry| {
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
        const file = try src_dir.openFile(file_path, .{});
        defer file.close();

        md5.update(file_path);

        var buffer: [4096]u8 = undefined;
        while (true) {
            const bytes_read = try file.read(&buffer);
            if (bytes_read == 0) break;
            md5.update(buffer[0..bytes_read]);
        }
    }

    var digest: [std.crypto.hash.Md5.digest_length]u8 = undefined;
    md5.final(&digest);

    const hex_chars = std.fmt.bytesToHex(digest, .lower);
    return try std.fmt.allocPrint(allocator, "\"{s}\"", .{&hex_chars});
}
pub fn build(b: *std.Build) !void
{
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
	
	parson.addCSourceFile(.{
		.file = dep_parson.path("parson.c"),
		.flags = &.{"-std=c89"},
	});
	
	parson.addIncludePath(dep_parson.path(""));
	parson.linkLibC();
	
	parson.installHeadersDirectory(dep_parson.path(""), "parson", .{});
	
	// sqlitecpp
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
	
	sqlitecpp.addIncludePath(dep_sqlitecpp.path("include"));
	sqlitecpp.addIncludePath(dep_sqlitecpp.path("sqlite3"));
	
	sqlitecpp.addCSourceFiles(.{
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
	sqlitecpp.linkLibC();
	sqlitecpp.linkLibCpp();
	
	
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
	
	// const dep_zstd_c = dep_mbedtls.builder.dependency("zstd", .{});
	
	// ixwebsocket
	
	const dep_ixwebsocket = b.dependency("ixwebsocket", .{
		.target = target,
		.optimize = .ReleaseFast,
	});
	
	// USE_TLS ON
	// USE_ZLIB ON
	const ixwebsocket = b.addLibrary(.{
		.name = "ixwebsocket",
		.root_module = b.createModule(.{
			.target = target,
			.optimize = .ReleaseFast,
			.link_libc = true,
		}),
	});
	
	ixwebsocket.addCSourceFiles(.{
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
			
			// USE_TLS > USE_MBED_TLS
			"ixwebsocket/IXSocketMbedTLS.cpp",
		}
	});
	
	var mbedtlsVersionGreaterThan3: bool = true;
	
	dep_mbedtls_c.path("").getPath3(b, null).access("include/mbedtls/build_info.h", .{}) catch {
		mbedtlsVersionGreaterThan3 = false;
	};
	
	ixwebsocket.root_module.addCMacro("IXWEBSOCKET_USE_TLS", "");
	ixwebsocket.root_module.addCMacro("IXWEBSOCKET_USE_MBED_TLS", "");
	if (mbedtlsVersionGreaterThan3)
	{
		ixwebsocket.root_module.addCMacro("IXWEBSOCKET_USE_MBED_TLS_MIN_VERSION_3", "");
	}
	
	ixwebsocket.addIncludePath(dep_ixwebsocket.path(""));
	ixwebsocket.addIncludePath(dep_ixwebsocket.path("ixwebsocket/"));
	ixwebsocket.addIncludePath(dep_mbedtls.path(""));
	if (target.result.os.tag == .linux)
	{
		ixwebsocket.root_module.addSystemIncludePath(.{.cwd_relative = "/usr/include/"});
	}
	ixwebsocket.linkLibC();
	ixwebsocket.linkLibCpp();
	ixwebsocket.linkLibrary(dep_mbedtls.artifact("mbedtls"));
	
	// SPSCQueue
	const dep_spscqueue = b.dependency("spscqueue", .{});
	
	// metamod
	const dep_metamod = b.dependency("metamod", .{});
	
	// hlsdk
	const dep_hlsdk = b.dependency("hlsdk", .{});

	// memtools (amxx)
	const memtools = b.addLibrary(.{
		.name = "memtools",
		.root_module = b.createModule(.{
			.target = target,
			.optimize = .ReleaseFast,
			.link_libc = true,
		}),
	});

	memtools.addIncludePath(b.path("deps/sdk/amxmodx/public"));
	if (target.result.os.tag == .windows)
	{
		memtools.root_module.addCMacro("WIN32", "");
	}
	memtools.addCSourceFiles(.{
		.files = &.{
			"deps/sdk/amxmodx/public/memtools/MemoryUtils.cpp",
			"deps/sdk/amxmodx/public/memtools/CDetour/detours.cpp",
		},
		.flags = &.{"-std=c++11", "-Wno-register"}
	});
	memtools.addCSourceFiles(.{.files = &.{"deps/sdk/amxmodx/public/memtools/CDetour/asm/asm.c"}});

	memtools.linkLibC();
	memtools.linkLibCpp();

	// the lib!!!!
	
	const lib = b.addLibrary(.{
		.name = if (target.result.os.tag == .linux) "kz_global_api_amxx_i386" else "kz_global_api_amxx",
		.linkage = .dynamic,
		.root_module = b.createModule(.{
			.target = target,
			.optimize = optimise,
			.link_libc = true,
		}),
	});

	
	lib.root_module.addCMacro("JIT", "");
	lib.root_module.addCMacro("ASM32", "");
	lib.root_module.addCMacro("HAVE_STDINT_H", "");
	lib.root_module.addCMacro("g_rehlds_available", "RehldsApi");

	const hash = try hashFilesInSrc(b.allocator);
	lib.root_module.addCMacro("MODULE_CHECKSUM", hash);
	defer b.allocator.free(hash);
	
	lib.linkLibrary(parson);
	lib.linkLibrary(sqlitecpp);
	lib.linkLibrary(ixwebsocket);
	lib.linkLibrary(memtools);
	lib.linkLibrary(dep_zstd.artifact("zstd"));
	
	if (target.result.os.tag == .windows)
	{
		if (comptime builtin.target.os.tag != .windows)
		{
			// Fixes windows cross compilation on linux.
			//  Amxmodx uses #include "Windows.h" like IDIOTS, so we
			//  must copy the windows.h file capitalised style, because windows's filesystem
			//  is case sensitive, while linux's (ext4 etc) are not.
			const windows_h_path = b.pathJoin(&[_][]const u8{std.fs.path.dirname(b.graph.zig_exe) orelse unreachable, "lib/libc/include/any-windows-any/windows.h"});
			var writeFile = b.addWriteFiles();
			_ = writeFile.addCopyFile(.{.cwd_relative = windows_h_path}, "Windows.h");
			lib.addIncludePath(writeFile.getDirectory());
		}
		
		lib.root_module.addCMacro("WIN32", "");
		lib.root_module.addCMacro("_WINDOWS", "");
		// HACK to get windows build to work
		lib.root_module.addCMacro("CBASE_DLLEXPORT", "__declspec(dllexport)");
		lib.linkSystemLibrary("ws2_32");
		lib.linkSystemLibrary("mswsock");
		lib.linkSystemLibrary("crypt32");
	}
	else if (target.result.os.tag == .linux)
	{
		if (std.fs.accessAbsolute("/usr/lib32/", .{}))
		{
			lib.root_module.addLibraryPath(.{.cwd_relative = "/usr/lib32/"});
		}
		else |_| undefined;
		
		if (std.fs.accessAbsolute("/usr/lib/i386-linux-gnu/", .{}))
		{
			lib.root_module.addLibraryPath(.{.cwd_relative = "/usr/lib/i386-linux-gnu/"});
		}
		else |_| undefined;
		
		lib.root_module.addSystemIncludePath(.{.cwd_relative = "/usr/include/"});
		
		lib.root_module.addCMacro("linux", "");
		lib.root_module.addCMacro("LINUX", "");
		lib.root_module.addCMacro("POSIX", "");
		lib.root_module.addCMacro("_LINUX", "");
		lib.linkSystemLibrary("sqlite3");
		lib.linkSystemLibrary("ssl");
		lib.linkSystemLibrary("crypto");
		lib.linkSystemLibrary("z");
		lib.linkSystemLibrary("pthread");
		lib.linkSystemLibrary("dl");
	}
	
	lib.addIncludePath(b.path("deps/sdk/amxmodx/public/resdk"));
	lib.addIncludePath(b.path("deps/sdk/amxmodx/public"));
	lib.addIncludePath(b.path("src/include"));
	lib.addIncludePath(dep_zstd.path("lib"));
	lib.addIncludePath(dep_ixwebsocket.path(""));
	lib.addIncludePath(dep_metamod.path("metamod"));
	lib.addIncludePath(dep_hlsdk.path(""));
	lib.addIncludePath(dep_hlsdk.path("common"));
	lib.addIncludePath(dep_hlsdk.path("dlls"));
	lib.addIncludePath(dep_hlsdk.path("engine"));
	lib.addIncludePath(dep_hlsdk.path("game_shared"));
	lib.addIncludePath(dep_hlsdk.path("public"));
	lib.addIncludePath(dep_hlsdk.path("pm_shared"));
	lib.addIncludePath(dep_sqlitecpp.path("include/"));
	lib.addIncludePath(dep_parson.path(""));
	lib.addIncludePath(dep_spscqueue.path("include"));
	lib.linkLibCpp();
	
	const cflagsBase = [_][]const u8{
		"-std=c++17",
		"-fno-sanitize=pointer-overflow", // fix for STRING() hlsdk macro
		"-fsanitize-recover=undefined",   // dont crash :)
	};
	const cflagsWarnings = [_][]const u8{
		// Enable warnings and escale to errors (zig doesnt catch warnings)
		"-Wall", "-Wextra", "-Werror",

		// Disable a bunch of hlsdk/amxx warnings
		"-Wno-unused-parameter",
		"-Wno-missing-field-initializers",  // (amxxmodule.cpp): missing field ... initializer
		"-Wno-deprecated-copy",             // (dlls/vector.h): implicit copy assignment operator .. is deprecated
	};
	var cflags = std.ArrayList([]const u8).empty;
	try cflags.appendSlice(b.allocator, &cflagsBase);
	try cflags.appendSlice(b.allocator, &cflagsWarnings);
	
	if (target.result.os.tag == .windows)
	{
		// HACK to get windows build to work
		try cflags.append(b.allocator, "-Wno-macro-redefined");
		try cflags.append(b.allocator, "-fpermissive");
	}
	
	lib.addCSourceFiles(.{
		.root = b.path("src/"),
		.files = &.{
			"amxxmodule.cpp",
			"kz_basic_ac.cpp",
			"kz_cvars.cpp",
			"kz_natives.cpp",
			"kz_replay.cpp",
			"kz_replay_pb.cpp",
			"kz_storage.cpp",
			"kz_util.cpp",
			"kz_ws.cpp",
			"kz_ws_msgs.cpp",
			"main.cpp",
			"mod_rehlds_api.cpp",
		},
		.flags = cflags.items,
	});

        var cdb_targets = std.ArrayListUnmanaged(*std.Build.Step.Compile){};
        try cdb_targets.append(b.allocator, lib);
        _ = zcc.createStep(b, "cdb", try cdb_targets.toOwnedSlice(b.allocator));

	b.installArtifact(lib);
}