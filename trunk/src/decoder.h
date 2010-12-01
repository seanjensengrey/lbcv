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

#ifndef _LBCV_DECODER_H_
#define _LBCV_DECODER_H_
#include "defs.h"
#include <lua.h>

/**
 * @file
 * The decoder is responsible for taking a stream of compiled bytecode, and
 * extracting from it sufficient information for the verifier to verify the
 * bytecode. Verification cannot be performed on the stream directly, as the
 * verifier needs to follow backward jumps and also needs parts of the bytecode
 * which are stored after the instruction list for the verification of the
 * instruction list.
 */

/**
 * Container for all the state required during the decoding process.
 */
struct decode_state
{
    /**
     * A Lua state to be passed to decode_state::reader.
     */
    lua_State* lua;
    /**
     * A function which is used to obtain the next chunk of bytes in the
     * stream of bytecode. Consult the Lua manual for the behaviour of reader
     * functions.
     */
    lua_Reader reader;
    /**
     * An opaque pointer passed to decode_state::reader.
     */
    void* readerud;
    /**
     * A function which is used for all memory allocation and deallocation.
     * Consult the Lua manual for the behaviour of allocator functions.
     */
    lua_Alloc alloc;
    /**
     * An opaque pointer passed to decode_state::alloc.
     */
    void* allocud;
    /**
     * Pointer to the next byte(s) in the stream of bytecode. The number of
     * bytes present at this address is stored in decode_state::chunklen. This
     * value will be the address most recently returned by the reader function,
     * incremented by some number of bytes, or @c NULL if the reader function
     * has yet to be called or has signaled the end of the stream.
     */
    const unsigned char* chunk;
    /**
     * The number of valid bytes present at decode_state::chunk.
     */
    size_t chunklen;
    /**
     * Indication of whether or not the bytecode stream uses the same
     * endianness as the machine which the verifier is being run on.
     * If @c true, the endianness is different, and if @c false, the endianness
     * is the same.
     */
    bool swapendian;
    /**
     * Indication of whether or not the bytecode stream stores integers in
     * little endian format.
     * If @c true, the endianness is little, and if @c false, the endianness
     * is big.
     */
    bool littleendian;
    /**
     * The number of bytes used to store an @c int in the bytecode stream.
     */
    size_t sizeint;
    /**
     * The number of bytes used to store a @c size_t in the bytecode stream.
     */
    size_t sizesize;
    /**
     * The number of bytes used to store a Lua virtual machine instruction in
     * the bytecode stream.
     */
    size_t sizeins;
    /**
     * The number of bytes used to store a Lua number in the bytecode stream.
     */
    size_t sizenum;
    /**
     * The current recursion depth of prototype decoding.
     */
    int level;
};
typedef struct decode_state decode_state_t;

/**
 * Container for all the information on a function prototype which the verifier
 * needs to verify that prototype.
 */
struct decoded_prototype
{
    /**
     * An array containing the virtual machine instructions.
     * Each record in this array is a decoded_prototype::instructionsize byte,
     * native endian integer, and there are decoded_prototype::numinstructions
     * records in total. This array will also have an extra @c int at the end,
     * to allow an instruction to be cast to an int without fear of reading
     * beyond the array.
     */
    unsigned char* code;
    /**
     * An array containing the type-code (e.g. @c LUA_TNUMBER) of each of the
     * constants in the constant type, one byte per constant.
     * The length of this array is given by decoded_prototype::numconstants.
     */
    unsigned char* constant_types;
    /**
     * An array containing the child prototypes.
     * The length of this array is given by decoded_prototype::numprototypes.
     */
    struct decoded_prototype** prototypes;
    /**
     * An array indicating the location of each of the prototype's upvalues.
     * The length of this array is given by decoded_prototype::numupvalues.
     * A @true indicates that the upvalue comes from a register of the creating
     * prototype, whereas a @false indicates that the upvalue comes from an
     * upvalue of the creating prototype. decoded_prototype::upvalue_index
     * gives the index of the register or upvalue.
     */
    bool* upvalue_instack;
    /**
     * An array indicating the index of each of the prototype's upvalues.
     * The length of this array is given by decoded_prototype::numupvalues.
     * @see decoded_prototype::upvalue_instack
     */
    unsigned char* upvalue_index;
    /**
     * The number of virtual machine instructions in decoded_prototype::code.
     */
    size_t numinstructions;
    /**
     * The number of bytes used per instruction in decoded_prototype::code.
     */
    size_t instructionsize;
    /**
     * The number of constants in the prototype's constant table, and the
     * length of decoded_prototype::constant_types.
     */
    size_t numconstants;
    /**
     * The number of child prototypes in decoded_prototype::prototypes.
     */
    size_t numprototypes;
    /**
     * The number of upvalues used by the prototype, and the length of the
     * decoded_prototype::upvalue_instack and decoded_prototype::upvalue_index
     * arrays.
     */
    size_t numupvalues;
    /**
     * The number of virtual machine registers used by the instructions of the
     * prototype.
     */
    unsigned int numregs;
    /**
     * The number of named parameters expected by the prototype.
     */
    unsigned int numparams;
    /**
     * Indication of whether or not the prototype accepts a variable length
     * argument list.
     */
    bool is_vararg;
};
typedef struct decoded_prototype decoded_prototype_t;

/**
 * Convert a stream of bytes containing the Lua 5.2 bytecode for a compiled
 * function into a structure which can be used by the verifier for verifying
 * that the function's bytecode isn't malicious.
 *
 * @param L A Lua state to be passed to @p reader, and whose memory allocator
 *          should be used for allocating the returned structure.
 * @param reader A function which will be called repeatedly to get chunks of
 *               the stream of bytes (see the Lua manual's description of a
 *               reader function).
 * @param dt An opaque pointer to be passed to @p reader.
 *
 * @return @c NULL if the stream of bytes didn't contain valid Lua 5.2 bytecode
 *         or an error occured during the decoding process. Otherwise, will be
 *         a valid pointer to a newly allocated decoded_prototype_t, which
 *         should later be freed by calling free_prototype().
 */
decoded_prototype_t* decode_bytecode(lua_State *L, lua_Reader reader,
                                     void* dt);

/**
 * Read a bytecode header and use it to fill in the appropriate parts of a
 * decode state.
 *
 * @param ds A decode_state_t whose reading stream is positioned at the start
 *           of a Lua 5.2 bytecode header. The reading stream will be advanced
 *           past the bytecode header, and the header will be used to fill in
 *           fields of the decode_state_t neccessary for decoding prototypes.
 *
 * @return @c true if a header was successfully decoded, @c false if decoding
 *         failed (in which which case the decode_state_t will not be in a
 *         state suitable for decoding prototypes).
 */
bool decode_header(decode_state_t* ds);

/**
 * Read and decode a single prototype (and all of its child prototypes) from a
 * decode state.
 *
 * @param ds A decode_state_t whose reading stream is positioned at the start
 *           of a dumped Lua 5.2 function. The reading stream will be advanced
 *           past the prototype.
 *
 * @return @c NULL if the reading stream didn't contain a valid Lua 5.2 dumped
 *         function, or an error occured during the decoding process. In this
 *         case, the reading stream will be left in an unspecified position.
 *         Otherwise, a valid pointer to a newly allocated decoded_prototype_t
 *         will be returned, which should later be freed by calling
 *         free_prototype().
 */
decoded_prototype_t* decode_prototype(decode_state_t* ds);

/**
 * Decode a single Lua 5.2 virtual machine instruction.
 *
 * @param proto The prototype containing the instruction to be decoded.
 * @param index The zero-based index into the prototype's instruction list of
 *              the instruction to be decoded.
 * @param op A pointer to a variable into which the decoded opcode field will
 *           be stored in the event of a successful decode.
 * @param a A pointer to a variable into which the decoded "A" or "Ax" field
 *          will be stored in the event of a successful decode.
 * @param b A pointer to a variable into which the decoded "B" or "Bx" or
 *          "sBx" field will be stored in the event of a successful decode, if
 *          the instruction had such a field.
 * @param c A pointer to a variable into which the decoded "C" field will be
 *          stored in the event of a successful decode, if the instruction had
 *          such a field.
 *
 * @return @c true if the specified instruction existed and was successfully
 *         decoded. Otherwise, @c false, which indicates malformed bytecode.
 */
bool decode_instruction(decoded_prototype_t* proto, size_t index, int* op,
                        int* a, int* b, int* c);

/**
 * Free the memory associated with a previously decoded prototype.
 *
 * @param proto The prototype to be freed.
 * @param alloc The allocator function to be used to free the memory; this
 *              should be compatible with the allocator used by the Lua state
 *              passed to decode_bytecode().
 * @param ud An opaque pointer which will be passed to @p alloc.
 */
void free_prototype(decoded_prototype_t* proto, lua_Alloc alloc, void* ud);

#endif /* _LBCV_DECODER_H_ */
