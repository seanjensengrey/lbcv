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
#if 0
#define LUAI_MAXCCALLS 200
#else
#include <llimits.h> /* or any file which defines LUAI_MAXCCALLS */
#endif

/*
    Note that if compiling this file with Microsoft Visual C++, in particular
    in debug mode, then edit-and-continue must be disabled, as this causes
    __LINE__ to no longer be constant. To do this, compile with /Zi rather than
    /ZI (e.g. "Debug Information Format" in project C++ properties shouldn't be
    set to "Program Database for Edit & Continue"). If this is not done, then
    the following error will be seen:
      error C2051: case expression not constant
*/

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
    if(sz != 0)
    {
        /* Read as much as possible from ds->chunk */
        size_t n = sz;
        if(n > ds->chunklen)
            n = ds->chunklen;
        if(dest)
        {
            memcpy(dest, ds->chunk, n);
            dest += n;
        }
        ds->chunk += n;
        ds->chunklen -= n;
        sz -= n;
        /* Yield if more input is required */
        if(sz != 0)
        {
            ds->readtarget = dest;
            ds->readlen = sz;
            return false;
        }
    }
    return true;
}

/**
 * Helper function to parse a single unsigned integer out of the bytes in the
 * buffer of a decode state.
 *
 * @param ds A decode_state_t whose buffer contains a read integer.
 * @param dest A pointer to a variable into which the read integer will be
 *             stored in the event of a successful parse.
 * @param sz The size, in bytes, of the integer in the buffer.
 *
 * @return @c false if the value of the integer was too large to fit in 
 * a @c size_t. @c true otherwise.
 */
bool parse_int(decode_state_t* ds, size_t* dest, size_t sz)
{
    size_t result = 0;
    if(!ds->swapendian && sz <= sizeof(size_t))
    {
        if(ds->littleendian)
            memcpy((void*)&result, (void*)ds->buffer, sz);
        else
            memcpy(((char*)&result)+sizeof(size_t)-sz, (void*)ds->buffer, sz);
    }
    if(ds->littleendian)
    {
        size_t shift = 0, n;
        for(n = 0; n < sz; ++n, shift += 8)
        {
            unsigned char c = ds->buffer[n];
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
            unsigned char c = ds->buffer[n];
            shifted = result << 8;
            if((shifted >> 8) != result)
                return false;
            result |= c;
        }
    }
    if(dest)
        *dest = result;
    return true;
}

#define HEADER_SIZE 18
#define TAIL "\x19\x93\r\n\x1a\n"

bool decode_header(decode_state_t* ds)
{
    unsigned char* header = ds->buffer, *p;
    unsigned int endian = 1;
    size_t insbits;

    /* Pull out and check the signature. */
    if(sizeof(LUA_SIGNATURE)-1 > HEADER_SIZE)
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
    if(ds->sizeint > sizeof(ds->buffer))
        return false;
    if(ds->sizesize > sizeof(ds->buffer))
        return false;

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
    if(HEADER_SIZE - (p - header) != sizeof(TAIL) - 1)
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

void byteswap(unsigned char* bytes, size_t n)
{
    size_t i = 0;
    for(--n; i < n; ++i, --n)
    {
        unsigned char I = bytes[i];
        unsigned char N = bytes[n];
        bytes[i] = N;
        bytes[n] = I;
    }
}

/**
 * Special value for decode_state::yieldpos indicating that the bytecode
 * header needs to be supplied and then subsequently decoded.
 */
#define DECODE_YIELDPOS_HEADER 0

/**
 * Special value for decode_state::yieldpos indicating that decoding has
 * finished without errors.
 */
#define DECODE_YIELDPOS_DONE 1

/**
 * The number of bytes required for a decode_state_t structure due to the
 * variably sized array at the end of the structure.
 */
#define SIZEOF_decode_state_t \
    (sizeof(decode_state_t) + sizeof(decoded_prototype_t*) * LUAI_MAXCCALLS)

decode_state_t* decode_bytecode_init(lua_Alloc alloc, void* allocud)
{
    decode_state_t* ds;
    if(HEADER_SIZE > sizeof(ds->buffer))
        return NULL;
    ds = (decode_state_t*)alloc(allocud, NULL, 0, SIZEOF_decode_state_t);
    if(ds != NULL)
    {
        ds->alloc = alloc;
        ds->allocud = allocud;
        ds->chunk = NULL;
        ds->chunklen = 0;
        ds->level = 0;
        /*  The header needs to be read. */
        ds->readlen = HEADER_SIZE;
        ds->readtarget = ds->buffer;
        ds->yieldpos = DECODE_YIELDPOS_HEADER;
    }
    return ds;
}

#define READ(dest, len) \
    if(!read(ds, dest, len)) \
        return ds->yieldpos = __LINE__, DECODE_YIELD; \
    case __LINE__:

#define READ_INT(dest, len) \
    if(!read(ds, ds->buffer, len)) \
        return ds->yieldpos = __LINE__, DECODE_YIELD; \
    case __LINE__: \
    if(!parse_int(ds, dest, len)) return DECODE_FAIL

#define SKIP_STRING_1() \
    if(!read(ds, ds->buffer, ds->sizesize)) \
        return ds->yieldpos = __LINE__, DECODE_YIELD; \
    case __LINE__: \
    if(!parse_int(ds, &ds->readlen, ds->sizesize)) return DECODE_FAIL

#define SKIP_STRING_2() \
    if(!read(ds, NULL, ds->readlen)) \
        return ds->yieldpos = __LINE__, DECODE_YIELD; \
    case __LINE__:

#define i ds->i

int decode_bytecode_pump(decode_state_t* ds, const unsigned char* pData, size_t iLength)
{
    decoded_prototype_t* proto;

    /* Continue the read operation which caused the yield. */
    ds->chunk = pData;
    ds->chunklen = iLength;
    if(!read(ds, ds->readtarget, ds->readlen))
        return DECODE_YIELD;

    proto = ds->stack[ds->level - 1];

    switch(ds->yieldpos)
    {
    case DECODE_YIELDPOS_HEADER:
        if(!decode_header(ds))
            return DECODE_FAIL;

        /* Main prototype decoding function */
ENTER_CHILD_PROTO:
        if(ds->level >= LUAI_MAXCCALLS)
            return DECODE_FAIL;
        proto = alloc_proto(ds);
        if(proto == NULL)
            return DECODE_ERROR_MEM;
        ds->stack[ds->level++] = proto;

        READ(NULL, ds->sizeint * 2);
        READ(ds->buffer, 3);

        proto->numparams = ds->buffer[0];
        proto->is_vararg = ds->buffer[1] != 0;
        proto->numregs = ds->buffer[2];

        /* Code */
        proto->instructionsize = ds->sizeins;
        READ_INT(&proto->numinstructions, ds->sizeint);
        if(proto->numinstructions == 0)
            return DECODE_FAIL;
        proto->code = (unsigned char*)ds->alloc(ds->allocud, NULL, 0,
            ds->sizeins * proto->numinstructions + sizeof(int));
        if(proto->code == NULL)
            return DECODE_ERROR_MEM;
        READ(proto->code, ds->sizeins * proto->numinstructions);
        if(ds->swapendian)
        {
            for(i = 0; i < proto->numinstructions; ++i)
                byteswap(proto->code + i * ds->sizeins, ds->sizeins);
        }

        /* Constants (excluding prototypes) */
        READ_INT(&proto->numconstants, ds->sizeint);
        proto->constant_types = (unsigned char*)ds->alloc(ds->allocud,
            NULL, 0, proto->numconstants);
        if(proto->numconstants != 0 && proto->constant_types == NULL)
            return DECODE_ERROR_MEM;
        for(i = 0; i < proto->numconstants; ++i)
        {
            unsigned char t;
            READ(proto->constant_types + i, 1);
            t = proto->constant_types[i];
            /* NB: Cannot use switch statement here, as possibly yielding reads
               cannot be in a nested swtich. */
            if(t == LUA_TSTRING)
            {
                SKIP_STRING_1();
                SKIP_STRING_2();
            }
            else if(t == LUA_TNUMBER)
            {
                READ(NULL, ds->sizenum);
            }
            else if(t == LUA_TBOOLEAN)
            {
                READ(ds->buffer, 1);
                if(ds->buffer[0] > 1)
                    return DECODE_FAIL;
            }
            else if(t != LUA_TNIL)
            {
                return DECODE_FAIL;
            }
        }

        /* Prototypes */
        READ_INT(&proto->numprototypes, ds->sizeint);
        proto->prototypes = (decoded_prototype_t**)ds->alloc(ds->allocud,
            NULL, 0, sizeof(decoded_prototype_t*) * proto->numprototypes);
        if(proto->numprototypes != 0 && proto->prototypes == NULL)
            return DECODE_ERROR_MEM;
        for(i = 0; i < proto->numprototypes; ++i)
            proto->prototypes[i] = NULL;
        for(i = 0; i < proto->numprototypes; ++i)
        {
            /* Recursively decode the child prototype.
              The loop counter needs to be saved somewhere, as it will be
              overwritten during the recursion. For this, the numupvalues field
              is used, as its value is not important at this stage of the
              decoding process. The result of the recursion is then pulled out
              of the stack and stored in the appropriate place. */
            proto->numupvalues = i;
            goto ENTER_CHILD_PROTO;
RESUME_PARENT_PROTO:
            i = proto->numupvalues;
            proto->prototypes[i] = ds->stack[ds->level];
        }

        /* Upvalues */
        READ_INT(&proto->numupvalues, ds->sizeint);
        proto->upvalue_instack = (bool*)ds->alloc(ds->allocud, NULL, 0,
            sizeof(bool) * proto->numupvalues);
        proto->upvalue_index = (unsigned char*)ds->alloc(ds->allocud, NULL, 0,
            proto->numupvalues);
        if((proto->upvalue_instack == NULL || proto->upvalue_index == NULL)
        && proto->numupvalues != 0)
            return DECODE_ERROR_MEM;
        for(i = 0; i < proto->numupvalues; ++i)
        {
            READ(ds->buffer, 2);
            proto->upvalue_instack[i] = ds->buffer[0] != 0;
            proto->upvalue_index[i] = ds->buffer[1];
        }

        /* Debug information */
        SKIP_STRING_1();
        SKIP_STRING_2();
        READ_INT(&i, ds->sizeint);
        READ(NULL, ds->sizeint * i);
        READ_INT(&i, ds->sizeint);
        for(; i > 0; --i)
        {
            SKIP_STRING_1();
            SKIP_STRING_2();
            READ(NULL, ds->sizeint * 2);
        }
        READ_INT(&i, ds->sizeint);
        for(; i > 0; --i)
        {
            SKIP_STRING_1();
            SKIP_STRING_2();
        }

        if(--ds->level == 0)
        {
            if(ds->chunklen != 0)
                return DECODE_FAIL;
            ds->yieldpos = DECODE_YIELDPOS_DONE;
            return DECODE_YIELD;
        }
        proto = ds->stack[ds->level - 1];
        goto RESUME_PARENT_PROTO;
        /* End of main prototype decoding function. */

    case DECODE_YIELDPOS_DONE:
        if(ds->chunklen == 0)
            return DECODE_YIELD;
        /* If this is being resumed, it means that there is spurious data
           beyond the end of the bytecode. In this case, the decoding should
           fail and not return a prototype, so the level field is set to 1 to
           ensure that the resulting prototype gets freed when the stack is
           freed. */
        ds->level = 1;
        return DECODE_FAIL;

    default:
        /* This should never happen, unless the yield/resume code is broken. */
        return DECODE_ERROR;
    }
}

#undef i
#undef READ
#undef READ_INT
#undef SKIP_STRING_1
#undef SKIP_STRING_2

decoded_prototype_t* decode_bytecode_finish(decode_state_t* ds)
{
    /* Get the return value, if there is one. */
    decoded_prototype_t* result = NULL;
    if(ds->yieldpos == DECODE_YIELDPOS_DONE && ds->level == 0)
        result = ds->stack[0];

    /* Free the stack. */
    while(ds->level != 0)
        free_prototype(ds->stack[--ds->level], ds->alloc, ds->allocud);

    /* Free the decode state itself. */
    ds->alloc(ds->allocud, ds, SIZEOF_decode_state_t, 0);

    return result;
}
