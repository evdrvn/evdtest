#!/bin/sh

LUALIBDIR=`pwd`
LUALIBDIR="$LUALIBDIR/../lua_modules"
LUA_PATH="$LUALIBDIR/share/lua/5.2/?.lua;../../lua/?.lua"
LUA_CPATH="$LUALIBDIR/lib/lua/5.2/?.so;"

export LUA_PATH
export LUA_CPATH

./minimal&
sleep 1
lua minimal.lua

