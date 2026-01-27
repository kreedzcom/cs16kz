
const std = @import("std");
const builtin = @import("builtin");
const assert = std.debug.assert;

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
		lib.root_module.addCMacro("WIN32", "");
		lib.root_module.addCMacro("_WINDOWS", "");
		// lib.linkSystemLibrary("curl.dll");
		// lib.linkSystemLibrary("shlwapi");
		// lib.linkSystemLibrary("imm32");
		// lib.linkSystemLibrary("gdi32");
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
	
	lib.addCSourceFiles(.{
		.root = b.path("src/"),
		.files = &.{
			"amxxmodule.cpp",
			"kz_basic_ac.cpp",
			"kz_cvars.cpp",
			"kz_natives.cpp",
			"kz_replay.cpp",
			"kz_storage.cpp",
			"kz_util.cpp",
			"kz_ws.cpp",
			"kz_ws_msgs.cpp",
			"main.cpp",
			"mod_rehlds_api.cpp",
		},
		.flags = &.{
			"-std=c++17",
			"-Wno-incompatible-pointer-types", // TODO: fix problems instead of disabling warning lol
		},
	});
	b.installArtifact(lib);
}
