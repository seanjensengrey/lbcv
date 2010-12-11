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
SOFTWARE.

Based in part on code from lbaselib.c in Lua 5.2, which is:
Copyright (C) 1994-2010 Lua.org, PUC-Rio.  All rights reserved.

*/

#define LUA_LIB
#include "decoder.h"
#include "verifier.h"
#include <lauxlib.h>
#include <string.h>

static int l_cleanup_decode_state(lua_State* L)
{
    decoded_prototype_t* proto;
    decode_state_t* ds = *(decode_state_t**)lua_touserdata(L, 1);
    if(ds)
    {
        proto = decode_bytecode_finish(ds);
        if(proto)
        {
            void* allocud;
            lua_Alloc alloc = lua_getallocf(L, &allocud);
            free_prototype(proto, alloc, allocud);
        }
    }
    return 0;
}

static int decode_fail(lua_State* L, int status)
{
    lua_pushnil(L);
    switch(status)
    {
    case DECODE_ERROR:
        lua_pushliteral(L, "unknown decoding error");
        break;

    case DECODE_ERROR_MEM:
        lua_pushliteral(L, "insufficient memory");
        break;

    default:
        lua_pushliteral(L, "unable to load bytecode");
        break;
    }
    return 2;
}

static int verify_fail(lua_State* L)
{
    lua_pushnil(L);
    lua_pushliteral(L, "verification failed");
    return 2;
}

static int not_string_err(lua_State* L)
{
    lua_pushliteral(L, "reader function must return a string");
    return lua_error(L);
}

static int l_verify(lua_State* L)
{
    decoded_prototype_t* proto = NULL;
    void* allocud;
    lua_Alloc alloc = lua_getallocf(L, &allocud);
    decode_state_t* ds = NULL;
    decode_state_t** pds = &ds;
    int status = DECODE_YIELD;
    size_t len;
    const char* str;

    if(lua_type(L, 1) != LUA_TSTRING)
    {
        if(lua_getctx(L, NULL) == LUA_OK)
        {
            /* If the reader function throws an error, then the decode state
              should still get cleaned. This is achieved by creating a userdata
              whose garbage collection metamethod cleans up the decode state.
              This also allows the decode state to be maintained if the reader
              function yields. */
            lua_settop(L, 1);
            pds = (decode_state_t**)lua_newuserdata(L, sizeof(decode_state_t*));
            lua_createtable(L, 0, 1);
            lua_pushcfunction(L, l_cleanup_decode_state);
            lua_setfield(L, 3, "__gc");
            lua_setmetatable(L, 2);
        }
        else
        {
            pds = (decode_state_t**)lua_touserdata(L, 2);
            ds = *pds;
            goto resume_continuation;
        }
    }

    ds = decode_bytecode_init(alloc, allocud);
    *pds = ds;
    if(lua_type(L, 1) == LUA_TSTRING)
    {
        str = lua_tolstring(L, 1, &len);
        status = decode_bytecode_pump(ds, str, len);
    }
    else
    {
        luaL_checktype(L, 1, LUA_TFUNCTION);
        while(status == DECODE_YIELD)
        {
            lua_settop(L, 2);
            lua_pushvalue(L, 1);
            lua_callk(L, 0, 1, 1, l_verify);
resume_continuation:
            str = lua_tolstring(L, 3, &len);
            if(str == NULL || len == 0)
            {
                if(str == NULL && lua_type(L, 3) != LUA_TNIL)
                    return not_string_err(L);
                break;
            }
            status = decode_bytecode_pump(ds, str, len);
        }
    }
    proto = decode_bytecode_finish(ds);
    *pds = NULL;
    if(proto == NULL)
    {
        return decode_fail(L, status);
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

static const char *checkrights(lua_State *L, const char *mode, const char *s)
{
    if(strchr(mode, 'b') == NULL && *s == LUA_SIGNATURE[0])
        return lua_pushliteral(L, "attempt to load a binary chunk");
    if(strchr(mode, 't') == NULL && *s != LUA_SIGNATURE[0])
        return lua_pushliteral(L, "attempt to load a text chunk");
    return NULL;  /* chunk in allowed format */
}

#define RESERVEDSLOT 4

/*
** Reader for generic `load' function: `lua_load' uses the
** stack for internal stuff, so the reader cannot change the
** stack top. Instead, it keeps its resulting string in a
** reserved slot inside the stack.
*/
typedef struct {  /* reader state */
  int f;  /* position of reader function on stack */
  const char *mode;  /* allowed modes (binary/text) */
  decode_state_t *ds; /* for binary chunks, the decode state */
  int decode_status; /* for binary chunks, result of decode_bytecode_pump */
} Readstat;

static const char *generic_reader(lua_State *L, void *ud, size_t *size)
{
    const char *s;
    size_t len;
    Readstat *stat = (Readstat *)ud;
    luaL_checkstack(L, 2, "too many nested functions");
    lua_pushvalue(L, stat->f);  /* get function */
    lua_call(L, 0, 1);  /* call it */
    if(lua_isnil(L, -1))
    {
        *size = 0;
        return NULL;
    }
    else if((s = lua_tolstring(L, -1, &len)) != NULL)
    {
        if(stat->mode != NULL)  /* first time? */
        {
            if(checkrights(L, stat->mode, s))  /* check mode */
                lua_error(L);
            stat->mode = NULL;  /* to avoid further checks */
            if(s[0] == LUA_SIGNATURE[0])
            {
                void* allocud;
                lua_Alloc alloc = lua_getallocf(L, &allocud);
                stat->ds = decode_bytecode_init(alloc, allocud);
            }
        }
        if(stat->ds)
        {
            int status = decode_bytecode_pump(stat->ds, s, len);
            if(status != DECODE_YIELD)
            {
                lua_pop(L, 1);
                decode_fail(L, status);
                lua_error(L);
            }
        }
        lua_replace(L, RESERVEDSLOT);  /* save string in reserved slot */
        return lua_tolstring(L, RESERVEDSLOT, size);
    }
    else
    {
        not_string_err(L);
        return NULL;  /* to avoid warnings */
    }
}

static int check_ds(lua_State *L, Readstat* stat)
{
    bool verified;
    void* allocud;
    lua_Alloc alloc = lua_getallocf(L, &allocud);
    decoded_prototype_t* proto = decode_bytecode_finish(stat->ds);
    if(proto == NULL)
        return decode_fail(L, stat->decode_status);
    verified = verify(proto, alloc, allocud);
    free_prototype(proto, alloc, allocud);
    if(!verified)
        return verify_fail(L);
    return 0;
}

static int l_load(lua_State *L)
{
    Readstat stat;
    size_t len;
    const char *str;
    void* allocud;
    lua_Alloc alloc = lua_getallocf(L, &allocud);
    decoded_prototype_t* proto = NULL;
    int status;
    stat.f = 1;
    stat.mode = luaL_optstring(L, 3, "bt");
    stat.ds = NULL;
    stat.decode_status = DECODE_YIELD;
    str = lua_tolstring(L, stat.f, &len);

    if(str == NULL)
    {
        /* Load from a reader function. */
        const char *chunkname = luaL_optstring(L, stat.f + 1, "=(load)");
        luaL_checktype(L, stat.f, LUA_TFUNCTION);
        lua_settop(L, RESERVEDSLOT);
        status = lua_load(L, generic_reader, (void*)&stat, chunkname);
        if(stat.ds)
        {
            if(check_ds(L, &stat) && status == LUA_OK)
                return 2;
        }
    }
    else
    {
        /* Load from a single string. */
        const char *chunkname = luaL_optstring(L, stat.f + 1, str);
        /* First check that mode is respected. */
        if(checkrights(L, stat.mode, str))
        {
            lua_pushnil(L);
            lua_insert(L, -2);
            return 2;
        }
        /* If it is bytecode, verify the bytecode before loading it. */
        if(str[0] == LUA_SIGNATURE[0])
        {
            stat.ds = decode_bytecode_init(alloc, allocud);
            stat.decode_status = decode_bytecode_pump(stat.ds, str, len);
            if(check_ds(L, &stat))
                return 2;
        }
        /* Do the actual loading. */
        status = luaL_loadbuffer(L, str, len, chunkname);
    }
    if (status == LUA_OK)
        return 1;
    else {
        lua_pushnil(L);
        lua_insert(L, -2);  /* put before error message */
        return 2;  /* return nil plus error message */
    }
}

const luaL_Reg lib[] = {
    {"verify", l_verify},
    {"load", l_load},
    {NULL, NULL}
};

LUAMOD_API int luaopen_lbcv(lua_State* L)
{
    lua_createtable(L, 0, sizeof(lib)/sizeof(*lib));
    luaL_setfuncs(L, lib, 0);
    return 1;
}
