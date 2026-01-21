#!/bin/bash

zig build -Dtarget=x86-linux-gnu
mv -f ./zig-out/lib/libkz_global_api_amxx_i386.so ./zig-out/lib/kz_global_api_amxx_i386.so
