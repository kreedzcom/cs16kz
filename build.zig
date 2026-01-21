
const std = @import("std");
const builtin = @import("builtin");
const assert = std.debug.assert;

pub fn build(b: *std.Build) !void
{
    const target = b.standardTargetOptions(.{});
	
	if (target.result.os.tag != .linux and target.result.os.tag != .windows)
	{
		std.debug.print("{} is not supported as a build target.\n", .{target.result.os.tag});
		// std.process.exit(1);
		return;
	}
	
	if (target.result.cpu.arch != .x86)
	{
		std.debug.print("Only x86 is supported as a build target.\n", .{});
		// std.process.exit(1);
		return;
	}
	
    const optimise = b.standardOptimizeOption(.{});
	
	// parson
	const parson = b.addLibrary(.{
		.name = "parson",
		.root_module = b.createModule(.{
			.target = target,
			.optimize = .ReleaseFast,
			.link_libc = true,
		}),
	});
	
	parson.addCSourceFile(.{
		.file = b.path("deps/parson/parson.c"),
		.flags = &.{"-std=c89"},
	});
	
	parson.addIncludePath(b.path("deps/parson/"));
	parson.linkLibC();
	
	// mbedtls
	const mbedtls = b.dependency("mbedtls", .{
		.target = target,
		.optimize = .ReleaseFast,
	});
	
	// sqlitecpp
	const sqlitecpp = b.addLibrary(.{
		.name = "sqlitecpp",
		.root_module = b.createModule(.{
			.target = target,
			.optimize = .ReleaseFast,
			.link_libc = true,
		}),
	});
	
	sqlitecpp.addIncludePath(b.path("deps/SQLiteCpp/include"));
	sqlitecpp.addIncludePath(b.path("deps/SQLiteCpp/sqlite3"));
	
	sqlitecpp.addCSourceFiles(.{
		.root = b.path("deps/SQLiteCpp/"),
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
	
	
	// ixwebsocket
	// USE_TLS ON
	// USE_ZLIB ON
	// -fexceptions, bruh?
	const ixwebsocket = b.addLibrary(.{
		.name = "ixwebsocket",
		.root_module = b.createModule(.{
			.target = target,
			.optimize = .ReleaseFast,
			.link_libc = true,
		}),
	});
	
	ixwebsocket.addCSourceFiles(.{
		.root = b.path("deps/ixwebsocket/"),
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
	std.fs.accessAbsolute("/usr/include/mbedtls/build_info.h", .{}) catch |err| {
		mbedtlsVersionGreaterThan3 = false;
		std.debug.print("Mbedtls older than v4 {}.\n", .{err});
	};
	
	ixwebsocket.root_module.addCMacro("IXWEBSOCKET_USE_TLS", "");
	ixwebsocket.root_module.addCMacro("IXWEBSOCKET_USE_MBED_TLS", "");
	if (mbedtlsVersionGreaterThan3)
	{
		ixwebsocket.root_module.addCMacro("IXWEBSOCKET_USE_MBED_TLS_MIN_VERSION_3", "");
	}
	
	ixwebsocket.addIncludePath(b.path("deps/ixwebsocket/"));
	if (target.result.os.tag == .linux)
	{
		ixwebsocket.root_module.addSystemIncludePath(.{.cwd_relative = "/usr/include/"});
	}
	ixwebsocket.linkLibC();
	ixwebsocket.linkLibCpp();
	ixwebsocket.linkLibrary(mbedtls.artifact("mbedtls"));
    
	// the lib!!!!
	
	const lib = b.addLibrary(.{
		.name = "kz_global_api_amxx_i386",
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
	
	lib.linkLibrary(parson);
	lib.linkLibrary(sqlitecpp);
	lib.linkLibrary(ixwebsocket);
	
	if (target.result.os.tag == .windows)
	{
		// lib.linkSystemLibrary("curl.dll");
		// lib.linkSystemLibrary("shlwapi");
		// lib.linkSystemLibrary("imm32");
		// lib.linkSystemLibrary("gdi32");
	}
	else if (target.result.os.tag == .linux)
	{
		// TODO: make configurable?
		lib.root_module.addLibraryPath(.{.cwd_relative = "/usr/lib32/"});
		// TODO: errors if dir doesn't exist, fix :(
		//lib.root_module.addLibraryPath(.{.cwd_relative = "/usr/lib/i386-linux-gnu/"});
		lib.root_module.addSystemIncludePath(.{.cwd_relative = "/usr/include/"});
		
		lib.root_module.addCMacro("linux", "");
		lib.linkSystemLibrary("sqlite3");
		lib.linkSystemLibrary("ssl");
		lib.linkSystemLibrary("crypto");
		lib.linkSystemLibrary("z");
		lib.linkSystemLibrary("pthread");
		lib.linkSystemLibrary("dl");
	}
	
	lib.addIncludePath(b.path("deps/sdk/metamod"));
	lib.addIncludePath(b.path("deps/sdk/amxmodx/public"));
	lib.addIncludePath(b.path("deps/sdk/amxmodx/public/resdk"));
	lib.addIncludePath(b.path("deps/ixwebsocket"));
	lib.addIncludePath(b.path("deps/SQLiteCpp/include/"));
	lib.addIncludePath(b.path("deps"));
	lib.addIncludePath(b.path("src/include"));
	lib.linkLibCpp();
	
	lib.addCSourceFiles(.{
		.root = b.path("src/"),
		.files = &.{
			"amxxmodule.cpp",
			"mod_rehlds_api.cpp",
			"kz_util.cpp",
			"kz_cvars.cpp",
			"kz_ws.cpp",
			"kz_ws_msgs.cpp",
			"kz_storage.cpp",
			"main.cpp",
		},
		.flags = &.{
			"-std=c++17",
			"-Wno-incompatible-pointer-types", // TODO: fix problems instead of disabling warning lol
		},
	});
	b.installArtifact(lib);
}
