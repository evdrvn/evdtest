#!/bin/sh

LUALIBDIR=`pwd`
LUALIBDIR="$LUALIBDIR/../lua_modules"
LUA_PATH="$LUALIBDIR/share/lua/5.2/?.lua;../../lua/?.lua"
LUA_CPATH="$LUALIBDIR/lib/lua/5.2/?.so;"

export LUA_PATH
export LUA_CPATH
export EVDTEST_TEST_CASE=test_minimal.lua

./minimal_test

