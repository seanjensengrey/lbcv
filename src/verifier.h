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

#ifndef _LBCV_VERIFIER_H_
#define _LBCV_VERIFIER_H_
#include <lua.h>
#include "defs.h"
#include "decoder.h"

/** Tracking of register state.
 * Every register in the register window of a prototype, at every point in the
 * instruction list, has zero or more of these flags set.
 */

/* The register can be read from and turned into an upvalue. */
#define REG_VALUEKNOWN  0x1

/* The register is an open upvalue. */
#define REG_OPENUPVALUE 0x2

/* The register definitely contains a table value. */
#define REG_ISTABLE     0x4

/* The register definitely contains a number value. */
#define REG_ISNUMBER    0x8

#define REG_TYPE_MASK (REG_ISTABLE | REG_ISNUMBER)

typedef unsigned int reg_index_t;

/**
 * Container for the state of every virtual machine register at a specific
 * point of execution.
 *
 * This structure should not be allocated on the stack, as it ends with a
 * variably sized array.
 *
 * The reg_state_* functions should be used for interacting with this type.
 */
struct reg_state
{
    /**
     * Lowest possible location of the "top" register marker.
     *
     * Generally this will be equal to the number of registers used by the
     * function, but it can be set to a lower value by instructions which
     * generate a variable number of results.
     */
    reg_index_t top_base;

    /**
     * The state of every virtual machine register.
     *
     * The state of each register is represented by a single byte, which will
     * be a combination of zero or more of the following flags: REG_VALUEKNOWN,
     * REG_OPENUPVALUE, REG_ISTABLE, REG_ISNUMBER.
     *
     * The length of this array will be decoded_prototype::numregs of the
     * verify_state::prototype of the enclosing verify_state_t.
     */
    unsigned char state_flags[1];
};
typedef struct reg_state reg_state_t;

bool reg_state_isknown(reg_state_t* state, reg_index_t reg);
bool reg_state_areknown(reg_state_t* state, reg_index_t reg, int num);
bool reg_state_isopen(reg_state_t* state, reg_index_t reg);
bool reg_state_areopen(reg_state_t* state, reg_index_t reg, int num);
bool reg_state_istable(reg_state_t* state, reg_index_t reg);
bool reg_state_isnumber(reg_state_t* state, reg_index_t reg);

void reg_state_setknown(reg_state_t* state, reg_index_t reg);
void reg_state_setopen(reg_state_t* state, reg_index_t reg);
void reg_state_settable(reg_state_t* state, reg_index_t reg);
void reg_state_setnumber(reg_state_t* state, reg_index_t reg);

void reg_state_unsetknown(reg_state_t* state, reg_index_t reg);
void reg_state_unsetopen(reg_state_t* state, reg_index_t reg);
void reg_state_unsettable(reg_state_t* state, reg_index_t reg);
void reg_state_unsetnumber(reg_state_t* state, reg_index_t reg);

/**
 * Simulate the value in one register being moved to another register.
 *
 * @param state The reg_state_t containing the registers to be simulated.
 * @param to The register into which the value will be moved.
 * @param from The register which the value will be moved (copied) from.
 *
 * @return @c true if the move was simulated, or @c false if the move is not
 *         permitted (for example moving an undefined value into a register
 *         which is an open upvalue).
 */
bool reg_state_move(reg_state_t* state, reg_index_t to, reg_index_t from);

/**
 * Simulate a value being assigned to a register.
 *
 * This will cause the specified register to be marked as having a known value,
 * and possible a value of the specified type (depending on whether said type
 * is one of the types which is tracked).
 *
 * @param state The reg_state_t containing the register to be simulated.
 * @param reg The index of the register to be assigned to.
 * @param type The Lua type code (eg. @c LUA_TNUMBER) of the value being
 *             assigned, or @c LUA_TNONE if the type is not known.
 */
void reg_state_assignment(reg_state_t* state, reg_index_t reg, int type);

/**
 * Container for the information needed for each individual virtual machine
 * instruction during the verification process.
 */
struct instruction_state
{
    /**
     * Indication of whether or not the instruction is in the list of
     * instructions which need tracing.
     *
     * This will be @c true if (and only if) the instruction is part of the
     * linked list starting at verify_state::next_to_trace.
     */
    bool needstracing;

    /**
     * Indication of whether or not static verification of the instruction
     * has been performed yet.
     *
     * Each instruction that might be executed will get statically verified
     * once, and will subsequently have this field set to @c true. If this
     * field is @c false, that means that the instruction either never gets
     * executed, or has not yet been visited at all by the tracing process.
     */
    bool seen;

    /**
     * State of the virtual machine registers prior to the execution of the
     * instruction.
     *
     * If there are multiple code paths to the instruction, then this will be
     * the state which is common across all code paths (it will start as the
     * state from one path, and then as subsequent paths are discovered, it
     * will be updated). If the instruction has not yet been visited by the
     * tracer, then this field will be @c NULL.
     */
    reg_state_t* regs;

    /**
     * Link to next instruction in the linked list of instructions which need
     * to be traced.
     *
     * If instruction_state::needstracing is @c false, then the value of this
     * field is undefined.
     */
    struct instruction_state* next_to_trace;
};
typedef struct instruction_state instruction_state_t;

/**
 * Container for all the information needed during the bytecode verification
 * process.
 *
 * This structure should not be allocated on the stack, as it ends with a
 * variable sized structure.
 */
struct verify_state
{
    /**
     * The prototype whose code is being verified.
     */
    decoded_prototype_t* prototype;

    /**
     * An array of instruction_state_t's, with one entry for each instruction
     * in the prototype's instruction list.
     */
    instruction_state_t* instruction_states;

    /**
     * A block of memory large enough for all the reg_state_t structures
     * required for the verification of the prototype and all (possibly
     * indirect) sub-prototypes.
     */
    unsigned char* reg_states;

    /**
     * Head of a linked list of instructions which need to be traced before
     * verification can be finished.
     *
     * Subsequent links are stored in instruction_state::next_to_trace.
     */
    instruction_state_t* next_to_trace;

    /**
     * The allocator function to be used to allocate and free memory used
     * during the verification process.
     */
    lua_Alloc alloc;

    /**
     * An opaque pointer to be passed to verify_state:alloc.
     */
    void* allocud;

    /**
     * Scratch space to be used to track the state of registers across the
     * simulation of a single virtual machine instruction.
     */
    reg_state_t next_regs;
};
typedef struct verify_state verify_state_t;

/**
 * Mark a range of registers as not having known values.
 *
 * This function is typically called when a range of registers are to be used
 * for a function call, as after the call the contents of the registers will be
 * undefined.
 *
 * @param vs The verify_state_t to which @p state belongs.
 * @param state The reg_state_t containing the range of registers to modify.
 * @param reg The index of the lowest register to modify. All registers at or
 *            above this index will be marked as not having known values.
 */
void reg_state_unsetknowntop(verify_state_t* vs, reg_state_t* state, reg_index_t reg);

/**
 * Simulate the setting of the "top" register marker.
 *
 * @param vs The verify_state_t to which @p state belongs.
 * @param state The reg_state_t to simulate the setting on.
 * @param base The index of the lowest register whose value might be set by
 *             the operation which sets "top".
 */
void reg_state_settop(verify_state_t* vs, reg_state_t* state,
                      reg_index_t base);

/**
 * Verify that the "top" register marker can be used as the endpoint of a range
 * of registers.
 *
 * @param state The register state to verify against.
 * @param base The index of a register.
 *
 * @return @c true if (and only if) [@p base, "top") defines a valid range of
 *         registers in the specified @p state, and each register in that range
 *         has a known value.
 */
bool reg_state_usetop(reg_state_t* state, reg_index_t base);

/**
 * Copy all of one reg_state_t structure to another such structure.
 *
 * @param vs The verify_state_t to which @p to and @p from belong.
 * @param to The reg_state_t to copy register state into.
 * @param from The reg_state_t to copy register state from.
 */

void reg_state_copy(verify_state_t* vs, reg_state_t* to, reg_state_t* from);

/**
 * Merge two reg_state_t structures together to create one that describes
 * the commonalities of both register states.
 *
 * @param vs The verify_state_t to which @p to and @p from belong.
 * @param to The reg_state_t to use as one of the merge sources, and to place
 *           the result into.
 * @param from The other merge source.
 *
 * @return 0 if the two merge sources are incompatible and could not be merged
 *         (for example if merging would produce a register which could be an
 *         open upvalue without a known value). 1 if merging was successful and
 *         resulted in a change to @p to. -1 if merging was successful but did
 *         not result in any changes to @p to.
 */
int reg_state_merge(verify_state_t* vs, reg_state_t* to, reg_state_t* from);

bool verify(decoded_prototype_t* prototype, lua_Alloc alloc, void* ud);

#endif /* _LBCV_VERIFIER_H_ */
