Lua 5.4.9, downloaded from https://www.lua.org/ftp/lua-5.4.9.tar.gz
SHA256: 2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6
License: MIT, reproduced in UPSTREAM-LICENSE.html and lua.h.

Vendored C sources/headers, excluding the standalone lua.c and luac.c tools.
Compiled by UnrealBuildTool inside ACEClient, without external runtime DLLs.
Local change: luaconf.h suppresses MSVC 4702 and Clang unreachable return/break
warnings for upstream's unreachable
return idioms, because Unreal treats that warning as an error.

ACEPluginVM loads only the explicitly allowed libraries. Never replace its
initialization with luaL_openlibs: that would expose filesystem/native loading.
