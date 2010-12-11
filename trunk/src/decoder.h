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
 * Return value from decode_bytecode_pump() indicating that decoding has not
 * encountered any problems, and and can continue given more input.
 */
#define DECODE_YIELD 0

/**
 * Return value from decode_bytecode_pump() indicating that decoding has
 * encountered invalid bytecode, and should not be supplied with further input.
 */
#define DECODE_FAIL 1

/**
 * Return value from decode_bytecode_pump() indicating that an internal error
 * occurred during the decoding process.
 */
#define DECODE_ERROR 2

/**
 * Return value from decode_bytecode_pump() indicating that insufficient memory
 * was available to perform the decoding process.
 */
#define DECODE_ERROR_MEM 3

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
 * Container for all the state required during the decoding process.
 *
 * This structure should not be allocated on the stack, as it ends with a
 * variably sized array.
 */
struct decode_state
{
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
     * bytes present at this address is stored in decode_state::chunklen.
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
    /**
     * When yielded, the number of bytes of input which need to be supplied in
     * order to be resumed.
     */
    size_t readlen;
    /**
     * When yielded, the buffer which input should be placed in prior to
     * resuming. This field may be @c NULL, indicating that input should be
     * read and then discarded.
     */
    unsigned char* readtarget;
    /**
     * When yielded, the position at which to resume.
     */
    size_t yieldpos;
    /**
     * When yielded, the value of the loop counter (if any).
     */
    size_t i;
    /**
     * A general-purpose buffer which can be used for reading small amounts.
     * This needs to be large enough for the header (currently 18 bytes),
     * miscellaneous small reads (currently up to 3 bytes), and integer fields
     * (currently at most 8 bytes on common architectures).
     */
    unsigned char buffer[32];
    /**
     * A stack containing all the prototypes which are currently in the process
     * of being decoded.
     * 
     * The number of elements in this stack is given by decode_state::level.
     *
     * The length of this array is variable, and determines the maximum
     * recursion depth of prototype decoding (which should be bounded anyway).
     */
    decoded_prototype_t* stack[1];
};
typedef struct decode_state decode_state_t;

/**
 * Initialise the bytecode decoding process.
 *
 * @param alloc An allocator function to be used to allocate all memory used
 *              during the decoding process.
 * @param allocud An opaque pointer which will be passed to @p alloc.
 *
 * @return @c NULL on memory allocation failure. Otherwise, a decode_state_t
 *         which can be used with decode_bytecode_pump() and must be freed by
 *         the caller via decode_bytecode_finish().
 */
decode_state_t* decode_bytecode_init(lua_Alloc alloc, void* allocud);

/**
 * Supply the next chunk of bytes containing Lua 5.2 bytecode to the bytecode
 * decoding process.
 *
 * @param ds A decode_state_t which is ready to consume some more input (which
 *           is true of freshly created states, and those for which prior calls
 *           to decode_bytecode_pump() have returned @c DECODE_YIELD).
 * @param pData Pointer to the next chunk of bytes in the Lua 5.2 bytecode
 *              stream being decoded.
 * @param iLength The number of bytes present at @p pData.
 *
 * @return One of @c DECODE_YIELD, @c DECODE_FAIL, @c DECODE_ERROR, or
 *         @c DECODE_ERROR_MEM. If @c DECODE_YIELD is returned, then the
 *         decoding process is so far successful, but needs the next chunk of
 *         bytes to continue decoding (which should be supplied in a subsequent
 *         call to decode_bytecode_pump). If @c DECODE_FAIL is returned, then
 *         the stream of bytes did not contain valid Lua 5.2 bytecode. If an
 *         error status is returned, then the decoding process failed, but not
 *         due to the bytecode being invalid.
 */
int decode_bytecode_pump(decode_state_t* ds, const unsigned char* pData, size_t iLength);

/**
 * Finish the bytecode decoding process, and free the associated state.
 *
 * @param ds A decode_state_t which has previously been created by calling
 *           decode_bytecode_init() and supplied with bytecode via calls to
 *           decode_bytecode_pump().
 *
 * @return If the bytecode supplied to the decode state was valid Lua 5.2
 *         bytecode for a single chunk, then a valid pointer to a
 *         decoded_prototype_t representing the chunk, which can be passed to
 *         verify(), and must at some point be freed by the caller with
 *         free_prototype(). In other cases, the return value is @c NULL.
 */
decoded_prototype_t* decode_bytecode_finish(decode_state_t* ds);

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
