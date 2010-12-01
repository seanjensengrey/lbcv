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

#include "decoder.h"
#include "opcodes.h"
#include <string.h>
#include <llimits.h> /* or any file which defines LUAI_MAXCCALLS */

/**
 * Helper function to extract a range of bits from an array of bytes.
 *
 * @param bytes An array of bytes containing an integer in native endianness,
 *              from which it is valid to read an @c int, or @p first + @p len
 *              bits, whichever is larger.
 * @param first The number of bits to discard at the little end of the integer.
 * @param len The number of bits to return after the discarded bits.
 * @return The specified range of bits, as an integer.
 */
int extract_bits(unsigned char* bytes, int first, int len)
{
    unsigned int result;

    if(first + len <= sizeof(int) * 8)
    {
        /* Simple case; read an entire int. */
        result = *(unsigned int*)bytes;
    }
    else
    {
        /* Complex case; read in each of the bytes containing bits in the
                         specified range. */
        int n;
        unsigned int endian = 0x1;

        result = 0;
        bytes += (first / 8);
        first %= 8;
        n = (first + len + 7) / 8;
        if((*((unsigned char*)(&endian))) == 1)
        {
            while(n > 0)
                result = (result << 8) | bytes[--n];
        }
        else
        {
            for(; n > 0; --n, ++bytes)
                result = (result << 8) | (*bytes);
        }
    }

    /* Get just the bits we're interested in from the bytes we have. */
    result >>= first;
    result &= ((1 << len) - 1);

    return (int)result;
}

bool decode_instruction(decoded_prototype_t* proto, size_t index, int* op,
                        int* a, int* b, int* c)
{
    unsigned char* ins;
    *op = -1;
    *a = -1;
    *b = -1;
    *c = -1;
    if(index >= proto->numinstructions)
        return false;

    ins = proto->code + proto->instructionsize * index;
    *op = extract_bits(ins, POS_OP, SIZE_OP);

    /* If the opcode isn't known, then the appropriate fields cannot be
       extracted. */
    if(*op < 0 || *op >= NUM_OPCODES)
        return false;

    switch(getOpMode(*op))
    {
    case iABC:
        *a = extract_bits(ins, POS_A, SIZE_A);
        *b = extract_bits(ins, POS_B, SIZE_B);
        *c = extract_bits(ins, POS_C, SIZE_C);
        break;

    case iABx:
        *a = extract_bits(ins, POS_A, SIZE_A);
        *b = extract_bits(ins, POS_Bx, SIZE_Bx);
        break;

    case iAsBx:
        *a = extract_bits(ins, POS_A, SIZE_A);
        *b = extract_bits(ins, POS_Bx, SIZE_Bx) - MAXARG_sBx;
        break;

    case iAx:
        *a = extract_bits(ins, POS_Ax, SIZE_Ax);
        break;
    }
    return true;
}

/**
 * Helper function to read a range of bytes from the reading stream of a decode
 * state.
 *
 * @param ds A decode_state_t to read bytes from.
 * @param dest A pointer to a block of memory into which the read bytes should
 *             be placed. This may be @c NULL, in which case the bytes will be
 *             read and discarded.
 * @param sz The number of bytes to read.
 *
 * @return @c true if the specified number of bytes was read, @false if less
 *         than the specified number was read.
 */
bool read(decode_state_t* ds, unsigned char* dest, size_t sz)
{
    while(sz > 0)
    {
        size_t n = sz;
        if(n > ds->chunklen)
            n = ds->chunklen;
        if(dest != NULL)
        {
            memcpy(dest, ds->chunk, n);
            dest += n;
        }
        sz -= n;
        ds->chunk += n;
        ds->chunklen -= n;
        if(ds->chunklen == 0)
        {
            ds->chunk = ds->reader(ds->lua, ds->readerud, &ds->chunklen);
            if(ds->chunk == NULL || ds->chunklen == 0)
            {
                ds->chunk = NULL;
                ds->chunklen = 0;
                if(sz > 0)
                    return false;
            }
        }
    }
    return true;
}

/**
 * Helper function to read a single unsigned integer from the reading stream of
 * a decode state.
 *
 * @param ds A decode_state_t to read from.
 * @param dest A pointer to a variable into which the read integer will be
 *             stored in the event of a successful read.
 * @param sz The size, in bytes, of the integer to read.
 *
 * @return @c false if the integer could not be read, or its value was too
 *         large to fit in a @c size_t. @c true otherwise.
 */
bool read_int(decode_state_t* ds, size_t* dest, size_t sz)
{
    size_t result = 0;
    if(!ds->swapendian && sz <= sizeof(size_t))
    {
        if(!read(ds, (void*)&result, sz))
            return false;
    }
    else if(ds->littleendian)
    {
        size_t shift = 0, n;
        for(n = 0; n < sz; ++n, shift += 8)
        {
            unsigned char c;
            if(!read(ds, &c, 1))
                return false;
            if(c != 0 && shift >= sizeof(result) * 8)
                return false;
            result |= (c << shift);
        }
    }
    else
    {
        size_t n, shifted;
        for(n = 0; n < sz; ++n)
        {
            unsigned char c;
            shifted = result << 8;
            if((shifted >> 8) != result)
                return false;
            if(!read(ds, &c, 1))
                return false;
            result |= c;
        }
    }
    if(dest)
        *dest = result;
    return true;
}

/**
 * Helper function to advance a decode state's read stream over a string.
 *
 * @param The decode state whose read stream should be advanced.
 *
 * @return @c true if successful, @c false on error.
 */
bool skip_string(decode_state_t* ds)
{
    size_t len;
    if(!read_int(ds, &len, ds->sizesize))
        return false;
    return read(ds, NULL, len);
}

#define TAIL "\x19\x93\r\n\x1a\n"

bool decode_header(decode_state_t* ds)
{
    unsigned char header[18], *p;
    unsigned int endian = 1;
    size_t insbits;

    /* Pull out and check the signature. */
    if(sizeof(LUA_SIGNATURE)-1 > sizeof(header))
        return false;
    if(!read(ds, header, sizeof(header)))
        return false;
    if(memcmp(header, LUA_SIGNATURE, sizeof(LUA_SIGNATURE)-1) != 0)
        return false;
    p = header + sizeof(LUA_SIGNATURE)-1;
    if(p[0] != 0x52)
        return false; /* Only Lua 5.2 bytecode is supported. */
    if(p[1] != 0)
        return false; /* Only official format bytecode is supported. */

    /* Pull out endianness and sizes. */
    ds->littleendian = 1 == (*(unsigned char*)&endian);
    ds->swapendian = p[2] != *(unsigned char*)&endian;
    ds->sizeint = p[3];
    ds->sizesize = p[4];
    ds->sizeins = p[5];
    if(ds->sizeint == 0 || ds->sizeins == 0)
        return false; /* Either of these being zero is absurd. */
    ds->sizenum = p[6];

    /* Check that the instruction size is large enough. */
    insbits = ds->sizeins * 8;
    if(POS_OP + SIZE_OP > insbits)
        return false;
    if(POS_A + SIZE_A > insbits)
        return false;
    if(POS_Ax + SIZE_Ax > insbits)
        return false;
    if(POS_B + SIZE_B > insbits)
        return false;
    if(POS_Bx + SIZE_Bx > insbits)
        return false;
    if(POS_C + SIZE_C > insbits)
        return false;

    /* Check the tail for encoding-related issues. */
    p += 8;
    if(sizeof(header) - (p - header) != sizeof(TAIL) - 1)
        return false;
    if(memcmp(p, TAIL, sizeof(TAIL) - 1) != 0)
        return false;

    return true;
}

decoded_prototype_t* alloc_proto(decode_state_t* ds)
{
    decoded_prototype_t* proto = (decoded_prototype_t*)ds->alloc(ds->allocud,
        NULL, 0, sizeof(decoded_prototype_t));
    if(proto != NULL)
    {
        proto->code = NULL;
        proto->constant_types = NULL;
        proto->prototypes = NULL;
        proto->upvalue_instack = NULL;
        proto->upvalue_index = NULL;
        proto->numinstructions = 0;
        proto->instructionsize = 0;
        proto->numconstants = 0;
        proto->numprototypes = 0;
        proto->numupvalues = 0;
        proto->numregs = 0;
        proto->numparams = 0;
        proto->is_vararg = false;
    }
    return proto;
}

void free_prototype(decoded_prototype_t* proto, lua_Alloc alloc, void* ud)
{
    if(proto == NULL)
        return;

    if(proto->code != NULL)
    {
        alloc(ud, (void*)proto->code, sizeof(int) + proto->numinstructions *
            proto->instructionsize, 0);
    }
    if(proto->constant_types != NULL)
        alloc(ud, (void*)proto->constant_types, proto->numconstants, 0);
    if(proto->prototypes)
    {
        size_t i;
        for(i = 0; i < proto->numprototypes; ++i)
            free_prototype(proto->prototypes[i], alloc, ud);
        alloc(ud, (void*)proto->prototypes, sizeof(decoded_prototype_t*) *
            proto->numprototypes, 0);
    }
    if(proto->upvalue_instack)
    {
        alloc(ud, (void*)proto->upvalue_instack, proto->numupvalues *
            sizeof(bool), 0);
    }
    if(proto->upvalue_index)
        alloc(ud, (void*)proto->upvalue_index, proto->numupvalues, 0);
    alloc(ud, (void*)proto, sizeof(decoded_prototype_t), 0);
}

/**
 * Helper function to call free_prototype from a decode_state_t and decrement
 * the level of the decode_state_t.
 *
 * @param ds A decode_state_t containing the allocator to use for freeing
 *           memory, and whose level will be decremented.
 * @param proto The prototype to be freed.
 *
 * @return @c NULL
 */
decoded_prototype_t* free_proto_ds(decode_state_t* ds,
                                   decoded_prototype_t* proto)
{
    free_prototype(proto, ds->alloc, ds->allocud);
    --ds->level;
    return NULL;
}

decoded_prototype_t* decode_prototype(decode_state_t* ds)
{
    decoded_prototype_t* proto;
    size_t i;
    unsigned char buff[4];

    if(ds->level >= LUAI_MAXCCALLS)
        return NULL;

    if(!read(ds, NULL, ds->sizeint * 2)) /* linedefined, lastlinedefined */
        return NULL;

    proto = alloc_proto(ds);
    if(!proto)
        return NULL;
    ++ds->level;
    if(!read(ds, buff, 3))
        return free_proto_ds(ds, proto);
    proto->numparams = buff[0];
    proto->is_vararg = buff[1] != 0;
    proto->numregs = buff[2];

    /* Code */
    proto->instructionsize = ds->sizeins;
    if(!read_int(ds, &proto->numinstructions, ds->sizeint))
        return free_proto_ds(ds, proto);
    if(proto->numinstructions == 0)
        return free_proto_ds(ds, proto);
    proto->code = (unsigned char*)ds->alloc(ds->allocud, NULL, 0, ds->sizeins *
        proto->numinstructions + sizeof(int));
    if(proto->code == NULL)
        return free_proto_ds(ds, proto);
    if(!read(ds, proto->code, ds->sizeins * proto->numinstructions))
        return free_proto_ds(ds, proto);
    /* TODO: byteswap each instruction to native endianness */

    /* Constants (excluding prototypes) */
    if(!read_int(ds, &proto->numconstants, ds->sizeint))
        return free_proto_ds(ds, proto);
    proto->constant_types = (unsigned char*)ds->alloc(ds->allocud, NULL, 0,
        proto->numconstants);
    if(proto->numconstants != 0 && proto->constant_types == NULL)
        return free_proto_ds(ds, proto);
    for(i = 0; i < proto->numconstants; ++i)
    {
        if(!read(ds, buff, 1))
            return free_proto_ds(ds, proto);
        proto->constant_types[i] = buff[0];
        switch(buff[0])
        {
        case LUA_TNIL:
            break;
        case LUA_TBOOLEAN:
            if(!read(ds, buff, 1))
                return free_proto_ds(ds, proto);
            if(buff[0] != 0 && buff[0] != 1)
                return free_proto_ds(ds, proto);
            break;
        case LUA_TNUMBER:
            if(!read(ds, NULL, ds->sizenum))
                return free_proto_ds(ds, proto);
            break;
        case LUA_TSTRING:
            if(!skip_string(ds))
                return free_proto_ds(ds, proto);
            break;
        default:
            return free_proto_ds(ds, proto);
        }
    }

    /* Prototypes */
    if(!read_int(ds, &proto->numprototypes, ds->sizeint))
        return free_proto_ds(ds, proto);
    proto->prototypes = (decoded_prototype_t**)ds->alloc(ds->allocud, NULL, 0,
        sizeof(decoded_prototype_t*) * proto->numprototypes);
    if(proto->numprototypes != 0 && proto->prototypes == NULL)
        return free_proto_ds(ds, proto);
    for(i = 0; i < proto->numprototypes; ++i)
        proto->prototypes[i] = NULL;
    for(i = 0; i < proto->numprototypes; ++i)
    {
        proto->prototypes[i] = decode_prototype(ds);
        if(proto->prototypes[i] == NULL)
            return free_proto_ds(ds, proto);
    }

    /* Upvalues */
    if(!read_int(ds, &proto->numupvalues, ds->sizeint))
        return free_proto_ds(ds, proto);
    proto->upvalue_instack = (bool*)ds->alloc(ds->allocud, NULL, 0,
        sizeof(bool) * proto->numupvalues);
    proto->upvalue_index = (unsigned char*)ds->alloc(ds->allocud, NULL, 0,
        proto->numupvalues);
    if((proto->upvalue_instack == NULL || proto->upvalue_index == NULL)
    && proto->numupvalues != 0)
        return free_proto_ds(ds, proto);
    for(i = 0; i < proto->numupvalues; ++i)
    {
        if(!read(ds, buff, 2))
            return free_proto_ds(ds, proto);
        proto->upvalue_instack[i] = buff[0] != 0;
        proto->upvalue_index[i] = buff[1];
    }

    /* Debug information */
    if(!skip_string(ds))
        return free_proto_ds(ds, proto);
    if(!read_int(ds, &i, ds->sizeint) || !read(ds, NULL, ds->sizeint * i))
        return free_proto_ds(ds, proto);
    if(!read_int(ds, &i, ds->sizeint))
        return free_proto_ds(ds, proto);
    for(; i > 0; --i)
    {
        if(!skip_string(ds) || !read(ds, NULL, ds->sizeint * 2))
            return free_proto_ds(ds, proto);
    }
    if(!read_int(ds, &i, ds->sizeint))
        return free_proto_ds(ds, proto);
    for(; i > 0; --i)
    {
        if(!skip_string(ds))
            return free_proto_ds(ds, proto);
    }

    --ds->level;
    return proto;
}

decoded_prototype_t* decode_bytecode(lua_State *L, lua_Reader reader, void* dt)
{
    decoded_prototype_t* proto;
    decode_state_t ds;
    unsigned char dummy;

    ds.lua = L;
    ds.reader = reader;
    ds.readerud = dt;
    ds.alloc = lua_getallocf(L, &ds.allocud);
    ds.chunk = NULL;
    ds.chunklen = 0;
    ds.level = 0;

    if(!decode_header(&ds))
        return NULL;
    proto = decode_prototype(&ds);
    if(proto != NULL && read(&ds, &dummy, 1))
    {
        /* Expected end of input. */
        return free_proto_ds(&ds, proto);
    }
    return proto;
}
