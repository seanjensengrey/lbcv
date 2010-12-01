/* Copyright (c) 2010 Peter Cawley

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE. */

#define LUA_LIB
#include "decoder.h"
#include "verifier.h"
#include <lauxlib.h>

struct load_string
{
    const char* str;
    size_t len;
};
typedef struct load_string load_string_t;
static const char* load_string_reader(lua_State *L, void* data, size_t* size)
{
    load_string_t* dt = (load_string_t*)data;
    const char* s = dt->str;
    if(size)
        *size = dt->len;
    dt->str = NULL;
    dt->len = 0;
    return s;
}

static const char* function_reader(lua_State *L, void* data, size_t* size)
{
    luaL_checkstack(L, 2, "too many nested functions");
    lua_pushvalue(L, 1);
    lua_call(L, 0, 1);
    if(lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        if(size)
            *size = 0;
    }
    else if(lua_tostring(L, -1) != NULL)
    {
        lua_replace(L, 2);
        return lua_tolstring(L, 2, size);
    }
    else
    {
        luaL_error(L, "reader function must return a string");
    }
    return NULL;
}

static int l_verify(lua_State* L)
{
    decoded_prototype_t* proto = NULL;
    void* allocud;
    lua_Alloc alloc = lua_getallocf(L, &allocud);
    if(lua_type(L, 1) == LUA_TSTRING)
    {
        load_string_t dt;
        dt.str = lua_tolstring(L, 1, &dt.len);
        proto = decode_bytecode(L, load_string_reader, (void*)&dt);
    }
    else
    {
        luaL_checktype(L, 1, LUA_TFUNCTION);
        lua_settop(L, 2);
        proto = decode_bytecode(L, function_reader, NULL);
    }
    if(proto == NULL)
    {
        lua_pushnil(L);
        lua_pushliteral(L, "unable to load bytecode");
        return 2;
    }
    else
    {
        bool good = verify(proto, alloc, allocud);
        free_prototype(proto, alloc, allocud);
        if(good)
        {
            lua_pushboolean(L, 1);
            return 1;
        }
        else
        {
            lua_pushnil(L);
            lua_pushliteral(L, "verification failed");
            return 2;
        }
    }
}

const luaL_Reg lib[] = {
    {"verify", l_verify},
    {NULL, NULL}
};

LUAMOD_API int luaopen_bytecodeverify(lua_State* L)
{
    lua_createtable(L, 0, sizeof(lib)/sizeof(*lib));
    luaL_setfuncs(L, lib, 0);
    return 1;
}
