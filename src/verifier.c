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

#include "verifier.h"
#include "opcodes.h"

bool reg_state_isknown(reg_state_t* state, reg_index_t reg)
{
    return (state->state_flags[reg] & REG_VALUEKNOWN) != 0;
}

bool reg_state_areknown(reg_state_t* state, reg_index_t reg, int num)
{
    for(--num; num >= 0; --num)
    {
        if(!reg_state_isknown(state, (reg_index_t)(reg + num)))
            return false;
    }
    return true;
}

bool reg_state_isopen(reg_state_t* state, reg_index_t reg)
{
    return (state->state_flags[reg] & REG_OPENUPVALUE) != 0;
}

bool reg_state_areopen(reg_state_t* state, reg_index_t reg, int num)
{
    for(--num; num >= 0; --num)
    {
        if(reg_state_isopen(state, (reg_index_t)(reg + num)))
            return true;
    }
    return false;
}

bool reg_state_istable(reg_state_t* state, reg_index_t reg)
{
    return (state->state_flags[reg] & REG_ISTABLE) != 0;
}

bool reg_state_isnumber(reg_state_t* state, reg_index_t reg)
{
    return (state->state_flags[reg] & REG_ISNUMBER) != 0;
}

void reg_state_setknown(reg_state_t* state, reg_index_t reg)
{
    state->state_flags[reg] |= REG_VALUEKNOWN;
}

void reg_state_setopen(reg_state_t* state, reg_index_t reg)
{
    state->state_flags[reg] |= REG_OPENUPVALUE;
    state->state_flags[reg] &=~ REG_TYPE_MASK;
}

void reg_state_settable(reg_state_t* state, reg_index_t reg)
{
    if(reg_state_isopen(state, reg))
        return;
    state->state_flags[reg] &=~ REG_TYPE_MASK;
    state->state_flags[reg] |= REG_ISTABLE | REG_VALUEKNOWN;
}

void reg_state_setnumber(reg_state_t* state, reg_index_t reg)
{
    if(reg_state_isopen(state, reg))
        return;
    state->state_flags[reg] &=~ REG_TYPE_MASK;
    state->state_flags[reg] |= REG_ISNUMBER | REG_VALUEKNOWN;
}

void reg_state_unsetknown(reg_state_t* state, reg_index_t reg)
{
    state->state_flags[reg] &=~ (REG_VALUEKNOWN | REG_TYPE_MASK);
}

void reg_state_unsetknowntop(verify_state_t* vs, reg_state_t* state, reg_index_t reg)
{
    for(; reg < vs->prototype->numregs; ++reg)
        reg_state_unsetknown(state, reg);
}

void reg_state_unsetopen(reg_state_t* state, reg_index_t reg)
{
    state->state_flags[reg] &=~ REG_OPENUPVALUE;
}

void reg_state_unsettable(reg_state_t* state, reg_index_t reg)
{
    state->state_flags[reg] &=~ REG_ISTABLE;
}

void reg_state_unsetnumber(reg_state_t* state, reg_index_t reg)
{
    state->state_flags[reg] &=~ REG_ISNUMBER;
}

int reg_state_merge(verify_state_t* vs, reg_state_t* to, reg_state_t* from)
{
    reg_index_t reg;
    bool anychanges = false;

    if(to->top_base > from->top_base)
    {
        to->top_base = from->top_base;
        anychanges = true;
    }

    for(reg = 0; reg < vs->prototype->numregs; ++reg)
    {
        unsigned char newflags = to->state_flags[reg] & from->state_flags[reg];
        if(((to->state_flags[reg] | from->state_flags[reg]) & REG_OPENUPVALUE) != 0)
        {
            newflags |= REG_OPENUPVALUE;
            if((newflags & REG_VALUEKNOWN) == 0)
                return 0; /* unable to merge */
            newflags &=~ REG_TYPE_MASK;
        }
        if(newflags != to->state_flags[reg])
            anychanges = true;
        to->state_flags[reg] = newflags;
    }
    return anychanges ? 1 : -1;
}

void reg_state_copy(verify_state_t* vs, reg_state_t* to, reg_state_t* from)
{
    reg_index_t reg;
    to->top_base = from->top_base;
    for(reg = 0; reg < vs->prototype->numregs; ++reg)
        to->state_flags[reg] = from->state_flags[reg];
}

bool reg_state_move(reg_state_t* state, reg_index_t to, reg_index_t from)
{
    if(to == from)
        return true;
    state->state_flags[to] &= REG_OPENUPVALUE; 
    state->state_flags[to] |= (state->state_flags[from] &~ REG_OPENUPVALUE);
    if((state->state_flags[to] & (REG_OPENUPVALUE | REG_VALUEKNOWN)) == REG_OPENUPVALUE)
        return false;
    return true;
}

void reg_state_assignment(reg_state_t* state, reg_index_t reg, int type)
{
    reg_state_setknown(state, reg);
    state->state_flags[reg] &=~ REG_TYPE_MASK;
    switch(type)
    {
    case LUA_TTABLE:
        reg_state_settable(state, reg);
        break;
    case LUA_TNUMBER:
        reg_state_setnumber(state, reg);
        break;
    }
}

void reg_state_settop(verify_state_t* vs, reg_state_t* state, reg_index_t base)
{
    reg_index_t i;
    state->top_base = (int)base;
    for(i = base; i < vs->prototype->numregs; ++i)
        state->state_flags[i] &=~ REG_TYPE_MASK;
}

bool reg_state_usetop(reg_state_t* state, reg_index_t base)
{
    if(state->top_base < (int)base)
        return false;
    for(; (int)base < state->top_base; ++base)
    {
        if(!reg_state_isknown(state, base))
            return false;
    }
    return true;
}

/**
 * Get the type of an RK (register / constant) field.
 *
 * @param vs The verify_state_t to which @p regs belongs, and whose prototype
             should be used for constant types.
 * @param regs Register state to take register type information from.
 * @param rk The value of an RK opcode field.
 *
 * @return The type code (eg. @c LUA_TNUMBER) of the register or constant
 *         referred to by @p rk, or @c LUA_TNONE if the type is not known.
 */
int rk_type(verify_state_t* vs, reg_state_t* regs, int rk)
{
    if(ISK(rk))
    {
        return vs->prototype->constant_types[rk - BITRK];
    }
    else
    {
        reg_index_t reg = (reg_index_t)rk;
        if(reg_state_isnumber(regs, reg))
            return LUA_TNUMBER;
        if(reg_state_istable(regs, reg))
            return LUA_TTABLE;
        return LUA_TNONE;
    }
}

bool check_next_op(verify_state_t* vs, instruction_state_t* ins, int opcode, int* a)
{
    int op;
    int b;
    int c;
    if(ins + 1 == vs->instruction_states + vs->prototype->numinstructions)
        return false;
    if(!decode_instruction(vs->prototype, ins - vs->instruction_states + 1, &op, a, &b, &c))
        return false;
    return op == opcode;
}

#define alloc_size(vs, n) ((vs)->alloc((vs)->allocud, NULL, 0, (n)))
#define alloc_vector(vs, typ, n) ((typ*)alloc_size(vs, (n) * sizeof(typ)))
#define alloc_one(vs, typ) alloc_vector(vs, typ, 1)
#define free_size(mem, vs, n) ((vs)->alloc((vs)->allocud, (mem), (n), 0))
#define free_vector(mem, vs, typ, n) free_size(mem, vs, (n) * sizeof(typ))
#define free_one(mem, vs, typ) free_vector(mem, vs, typ, 1)

bool verify_next(verify_state_t* vs, instruction_state_t* ins, int offset)
{
    ++offset; /* make relative to ins, rather than next-pc */

    if(!ins->seen)
    {
        int ins_off = (int)(ins - vs->instruction_states);
        if(offset < 0)
        {
            offset = -offset;
            if(offset <= 0)
                return false;
            if(offset > ins_off)
                return false;
            offset = -offset;
        }
        else
        {
            if((size_t)(ins_off + offset) >= vs->prototype->numinstructions)
                return false;
        }
    }

    ins += offset;
    if(ins->regs == NULL)
    {
        ins->regs = (reg_state_t*)alloc_size(vs, sizeof(reg_state_t) + vs->prototype->numregs);
        if(ins->regs == NULL)
            return false;
        reg_state_copy(vs, ins->regs, &vs->next_regs);
    }
    else
    {
        switch(reg_state_merge(vs, ins->regs, &vs->next_regs))
        {
        case 0:
            return false;
        case -1:
            return true; /* nothing to do */
        default:
            break;
        }
    }

    if(!ins->needstracing)
    {
        ins->needstracing = true;
        ins->next_to_trace = vs->next_to_trace;
        vs->next_to_trace = ins;
    }

    return true;
}

bool is_reg_valid(verify_state_t* vs, int reg)
{
    return reg >= 0 && (unsigned int)reg < vs->prototype->numregs;
}

bool is_k_valid(verify_state_t* vs, int k)
{
    return k >= 0 && (size_t)k < vs->prototype->numconstants;
}

bool is_upvalue_valid(verify_state_t* vs, int upvalue)
{
    return upvalue >= 0 && (size_t)upvalue < vs->prototype->numupvalues;
}

bool verify_static(verify_state_t* vs, instruction_state_t* ins, int op, int a,
                   int b, int c)
{
    if(op < 0 || op >= NUM_OPCODES)
        return false;
    if(testTMode(op) != 0)
    {
        int dummy;
        if(!check_next_op(vs, ins, OP_JMP, &dummy))
            return false;
    }
    if(testAMode(op) != 0)
    {
        if(!is_reg_valid(vs, a))
            return false;
    }
    switch(getBMode(op))
    {
    case OpArgK:
        if(getOpMode(op) == iABx)
            break;
        if(ISK(b))
        {
            if(!is_k_valid(vs, INDEXK(b)))
                return false;
            break;
        }
        /* fallthrough */

    case OpArgR:
        if(getOpMode(op) != iAsBx && !is_reg_valid(vs, b))
            return false;
        break;
    }
    switch(getCMode(op))
    {
    case OpArgK:
        if(ISK(c))
        {
            if(!is_k_valid(vs, INDEXK(c)))
                return false;
            break;
        }
        /* fallthrough */

    case OpArgR:
        if(!is_reg_valid(vs, c))
            return false;
        break;
    }
    switch(op)
    {
    case OP_LOADK:
        if(b == 0)
        {
            int k;
            if(!check_next_op(vs, ins, OP_EXTRAARG, &k))
                return false;
            if(!is_k_valid(vs, k))
                return false;
        }
        else
        {
            if(!is_k_valid(vs, b - 1))
                return false;
        }
        break;

    case OP_LOADBOOL:
        if(b != 0 && b != 1)
            return false;
        break;

    case OP_LOADNIL:
        if(!is_reg_valid(vs, b))
            return false;
        if(b < a)
            return false;
        break;

    case OP_GETUPVAL:
    case OP_GETTABUP:
    case OP_SETUPVAL:
        if(!is_upvalue_valid(vs, b))
            return false;
        break;

    case OP_SETTABUP:
        if(!is_upvalue_valid(vs, a))
            return false;
        break;

    case OP_SELF:
        if(!is_reg_valid(vs, a + 1))
            return false;
        if(ISK(c))
        {
            if(!is_k_valid(vs, INDEXK(c)))
                return false;
        }
        else if(!is_reg_valid(vs, c))
            return false;
        break;

    case OP_CONCAT:
        if(c <= b)
            return false;
        break;

    case OP_CALL:
        if(c >= 3)
        {
            if(!is_reg_valid(vs, a + c - 2))
                return false;
        }
        /* fallthrough */

    case OP_TAILCALL:
        if(b >= 2)
        {
            if(!is_reg_valid(vs, a + b - 1))
                return false;
        }
        break;

    case OP_TFORLOOP:
        if(!is_reg_valid(vs, a + 1))
            return false;
        break;

    case OP_RETURN:
        if(b != 1 && !is_reg_valid(vs, a))
            return false;
        goto OP_VARARG_fallthrough;

    case OP_VARARG:
        if(!vs->prototype->is_vararg)
            return false;
OP_VARARG_fallthrough:
        if(b >= 3)
        {
            if(!is_reg_valid(vs, a + b - 2))
                return false;
        }
        break;

    case OP_TFORCALL:
        if(!is_reg_valid(vs, a + 2 + c))
            return false;
        /* fallthrough */

    case OP_FORLOOP:
        if(!is_reg_valid(vs, a + 3))
            return false;
        break;

    case OP_FORPREP:
        if(!is_reg_valid(vs, a + 2))
            return false;
        break;

    case OP_SETLIST:
        if(!is_reg_valid(vs, a))
            return false;
        if(c == 0)
        {
            int dummy;
            if(!check_next_op(vs, ins, OP_EXTRAARG, &dummy))
                return false;
        }
        break;

    case OP_CLOSE:
        if(!is_reg_valid(vs, a))
            return false;
        break;

    case OP_CLOSURE:
        if(b < 0 || (size_t)b >= vs->prototype->numprototypes)
            return false;
        {
            decoded_prototype_t* proto = vs->prototype->prototypes[b];
            size_t i;
            for(i = 0; i < proto->numupvalues; ++i)
            {
                if(proto->upvalue_index[i] < 0)
                        return false;
                if(proto->upvalue_instack[i] == 0)
                {
                    if(!is_upvalue_valid(vs, proto->upvalue_index[i]))
                        return false;
                }
                else
                {
                    if(!is_reg_valid(vs, proto->upvalue_index[i]))
                        return false;
                }
            }
        }
        break;
    }
    return true;
}

bool simulate_instruction(verify_state_t* vs, instruction_state_t* ins, int op,
                          int a, int b, int c)
{
    reg_state_copy(vs, &vs->next_regs, ins->regs);
    vs->next_regs.top_base = -1;

    /* Common behaviour: reading from R(B) or R(C) */
    if(getOpMode(op) == iABC)
    {
        if((getBMode(op) == OpArgR) || (getBMode(op) == OpArgK && !ISK(b)))
        {
            if(!reg_state_isknown(ins->regs, (reg_index_t)b))
                return false;
        }
        if((getCMode(op) == OpArgR) || (getCMode(op) == OpArgK && !ISK(c)))
        {
            if(!reg_state_isknown(ins->regs, (reg_index_t)c))
                return false;
        }
    }

    switch(op)
    {
    case OP_MOVE:
        if(!reg_state_move(&vs->next_regs, (reg_index_t)a, (reg_index_t)b))
            return false;
        break;

    case OP_LOADK:
        if(b == 0)
        {
            decode_instruction(vs->prototype, 1 + (size_t)(ins -
                vs->instruction_states), &c, &b, &c, &c);
            ++b;
        }
        reg_state_assignment(&vs->next_regs, (reg_index_t)a,
            vs->prototype->constant_types[b-1]);
        break;

    case OP_LOADNIL:
        for(; b >= a; --b)
            reg_state_assignment(&vs->next_regs, (reg_index_t)b, LUA_TNIL);
        break;

    case OP_SETTABLE:
        if(!reg_state_isknown(ins->regs, (reg_index_t)a))
            return false;
        break;

    case OP_NEWTABLE:
        reg_state_settable(&vs->next_regs, (reg_index_t)a);
        break;

    case OP_ADD:
    case OP_SUB:
    case OP_MUL:
    case OP_DIV:
    case OP_MOD:
    case OP_POW:
        reg_state_setknown(&vs->next_regs, (reg_index_t)a);
        reg_state_unsettable(&vs->next_regs, (reg_index_t)a);
        if(rk_type(vs, ins->regs, b) == LUA_TNUMBER
        && rk_type(vs, ins->regs, c) == LUA_TNUMBER)
            reg_state_setnumber(&vs->next_regs, (reg_index_t)a);
        else
            reg_state_unsetnumber(&vs->next_regs, (reg_index_t)a);
        break;
    
    case OP_UNM:
        reg_state_setknown(&vs->next_regs, (reg_index_t)a);
        reg_state_unsettable(&vs->next_regs, (reg_index_t)a);
        if(reg_state_isnumber(ins->regs, (reg_index_t)b))
            reg_state_setnumber(&vs->next_regs, (reg_index_t)a);
        else
            reg_state_unsetnumber(&vs->next_regs, (reg_index_t)a);
        break;

    case OP_CONCAT:
        if(!reg_state_areknown(ins->regs, b, c - b + 1))
            return false;
        reg_state_assignment(&vs->next_regs, (reg_index_t)a, LUA_TNONE);
        break;

    case OP_TEST:
        if(!reg_state_isknown(ins->regs, (reg_index_t)a))
            return false;
        break;

    case OP_CALL:
        reg_state_unsetknowntop(vs, &vs->next_regs, (reg_index_t)(a+1));
        if(c == 0)
            reg_state_settop(vs, &vs->next_regs, (reg_index_t)a);
        else
        {
            for(c -= 2; c >= 0; --c)
                reg_state_assignment(&vs->next_regs, (reg_index_t)(a+c),
                    LUA_TNONE);
        }
        goto OP_TAILCALL_fallthrough;

    case OP_TAILCALL:
        reg_state_unsetknowntop(vs, &vs->next_regs, (reg_index_t)(a+1));
        reg_state_settop(vs, &vs->next_regs, (reg_index_t)a);
OP_TAILCALL_fallthrough:
        if(b == 0)
        {
            if(!reg_state_usetop(ins->regs, (reg_index_t)(a+1)))
                return false;
            if(!reg_state_isknown(ins->regs, (reg_index_t)a))
                return false;
        }
        else
        {
            if(!reg_state_areknown(ins->regs, (reg_index_t)a, b))
                return false;
        }
        if(reg_state_areopen(ins->regs, (reg_index_t)a, vs->prototype->numregs - a))
            return false;
        break;

    case OP_RETURN:
        if(b == 0)
        {
            if(!reg_state_usetop(ins->regs, (reg_index_t)a))
                return false;
        }
        else
        {
            if(!reg_state_areknown(ins->regs, (reg_index_t)a, b - 1))
                return false;
        }
        break;

    case OP_FORLOOP:
        if(!reg_state_isnumber(ins->regs, (reg_index_t)a))
            return false;
        if(!reg_state_isnumber(ins->regs, (reg_index_t)(a+1)))
            return false;
        if(!reg_state_isnumber(ins->regs, (reg_index_t)(a+2)))
            return false;
        break;

    case OP_FORPREP:
        for(c = 0; c < 3; ++c)
        {
            if(!reg_state_isknown(ins->regs, (reg_index_t)(a+c)))
                return false;
            /* There is a runtime check that the value is a number. */
            reg_state_setnumber(&vs->next_regs, (reg_index_t)(a+c));
        }
        break;

    case OP_TFORCALL:
        reg_state_unsetknowntop(vs, &vs->next_regs, (reg_index_t)(a+4));
        if(reg_state_areopen(ins->regs, (reg_index_t)(a+3), vs->prototype->numregs - a - 3))
            return false;
        if(!reg_state_areknown(ins->regs, (reg_index_t)a, 3))
            return false;
        for(c += 2; c >= 3; --c)
            reg_state_assignment(&vs->next_regs, (reg_index_t)(a+c), LUA_TNONE);
        /* fallthrough */

    case OP_TFORLOOP:
        if(!reg_state_isknown(ins->regs, (reg_index_t)(a+1)))
            return false;
        break;

    case OP_SETLIST:
        if(!reg_state_istable(ins->regs, (reg_index_t)a))
            return false;
        if(b == 0)
        {
            if(!reg_state_usetop(ins->regs, (reg_index_t)a))
                return false;
        }
        if(!reg_state_areknown(ins->regs, (reg_index_t)(a+1), b))
            return false;
        break;

    case OP_CLOSE:
        for(; (size_t)a < vs->prototype->numregs; ++a)
            reg_state_unsetopen(&vs->next_regs, (reg_index_t)a);
        break;

    case OP_CLOSURE:
        {
            decoded_prototype_t* proto = vs->prototype->prototypes[b];
            size_t i;
            reg_state_assignment(&vs->next_regs, (reg_index_t)a, LUA_TFUNCTION);
            for(i = 0; i < proto->numupvalues; ++i)
            {
                if(!proto->upvalue_instack[i])
                    continue;
                /* Uses vs->next_regs, rather than ins->regs, as the newly
                 created closure might be used as an upvalue. */
                if(!reg_state_isknown(&vs->next_regs, proto->upvalue_index[i]))
                    return false;
                reg_state_setopen(&vs->next_regs, proto->upvalue_index[i]);
            }
        }
        break;

    case OP_VARARG:
        if(b == 0)
            reg_state_settop(vs, &vs->next_regs, (reg_index_t)a);
        for(b -= 2; b >= 0; --b)
            reg_state_assignment(&vs->next_regs, (reg_index_t)(a+b), LUA_TNONE);
        break;

    case OP_SELF:
        if(!reg_state_move(&vs->next_regs, (reg_index_t)(a+1), (reg_index_t)b))
            return false;
        if(!ISK(c))
        {
            if(!reg_state_isknown(&vs->next_regs, (reg_index_t)c))
                return false;
        }
        /* fallthrough */

    default:
        if(testAMode(op) != 0)
            reg_state_assignment(&vs->next_regs, (reg_index_t)a, LUA_TNONE);
        break;
    }
    
    return true;
}

bool schedule_next(verify_state_t* vs, instruction_state_t* ins, int op, int a,
                   int b, int c)
{
    switch(op)
    {
    case OP_LOADBOOL:
        if(!verify_next(vs, ins, (c != 0) ? 1 : 0))
            return false;
        break;

    case OP_RETURN:
        break;

    case OP_TESTSET:
        if(!verify_next(vs, ins, 1))
            return false;
        if(!reg_state_move(&vs->next_regs, (reg_index_t)a, (reg_index_t)b))
            return false;
        if(!verify_next(vs, ins, 0))
            return false;
        break;

    case OP_FORLOOP:
        if(!verify_next(vs, ins, 0))
            return false;
        if(!reg_state_move(&vs->next_regs, (reg_index_t)(a+3), (reg_index_t)a))
            return false;
        goto next_default_fallthrough;

    case OP_TFORLOOP:
        if(!verify_next(vs, ins, 0))
            return false;
        if(!reg_state_move(&vs->next_regs, (reg_index_t)a, (reg_index_t)(a+1)))
            return false;
        goto next_default_fallthrough;

    default:
next_default_fallthrough:
        if(testTMode(op) != 0)
        {
            if(!verify_next(vs, ins, 1))
                return false;
        }

        if(!verify_next(vs, ins, (getOpMode(op) == iAsBx) ? b : 0))
            return false;
        break;
    }
    return true;
}

bool verify_step(verify_state_t* vs)
{
    int op, a, b, c;
    instruction_state_t* ins = vs->next_to_trace;
    vs->next_to_trace = ins->next_to_trace;
    if(!decode_instruction(vs->prototype, (size_t)(ins - vs->instruction_states), &op, &a, &b, &c))
        return false;

    if(!ins->seen && !verify_static(vs, ins, op, a, b, c))
        return false;

    if(!simulate_instruction(vs, ins, op, a, b, c))
        return false;    

    if(!schedule_next(vs, ins, op, a, b, c))
        return false;

    ins->seen = true;
    ins->needstracing = false;
    return true;
}

bool verify(decoded_prototype_t* prototype, lua_Alloc alloc, void* ud)
{
    size_t i;
    bool allgood = true;
    verify_state_t* vs = (verify_state_t*)alloc(ud, NULL, 0, sizeof(verify_state_t) + prototype->numregs);
    if(vs == NULL)
        return false;

    vs->prototype = prototype;
    vs->alloc = alloc;
    vs->allocud = ud;
    if(prototype->numinstructions == 0)
        allgood = false;
    if(prototype->numparams > prototype->numregs)
        allgood = false;
    vs->instruction_states = alloc_vector(vs, instruction_state_t, prototype->numinstructions);
    if(vs->instruction_states == NULL)
        allgood = false;

    if(allgood)
    {
        for(i = 0; i < prototype->numinstructions; ++i)
        {
            vs->instruction_states[i].needstracing = false;
            vs->instruction_states[i].seen = false;
            vs->instruction_states[i].regs = NULL;
            vs->instruction_states[i].next_to_trace = NULL;
        }
        vs->instruction_states[0].regs = (reg_state_t*)alloc_size(vs, sizeof(reg_state_t) + prototype->numregs);
        if(vs->instruction_states[0].regs == NULL)
            allgood = false;
    }
    
    if(allgood)
    {
        vs->instruction_states[0].regs->top_base = -1;
        for(i = 0; i < prototype->numregs; ++i)
        {
            vs->instruction_states[0].regs->state_flags[i] = 0;
            if(i < prototype->numparams)
                reg_state_setknown(vs->instruction_states[0].regs, i);
        }
        vs->instruction_states[0].needstracing = true;
        vs->next_to_trace = vs->instruction_states;

        while(allgood && vs->next_to_trace)
            allgood = verify_step(vs);
    }

    /* Cleanup */
    for(i = 0; i < prototype->numinstructions; ++i)
    {
        if(vs->instruction_states[i].regs)
            free_size(vs->instruction_states[i].regs, vs, sizeof(reg_state_t) + prototype->numregs);
    }
    free_vector(vs->instruction_states, vs, instruction_state_t, prototype->numinstructions);

    alloc(ud, (void*)vs, sizeof(verify_state_t) + prototype->numregs, 0);

    /* Recursively verify children */
    for(i = 0; allgood && i < prototype->numprototypes; ++i)
        allgood = verify(prototype->prototypes[i], alloc, ud);

    return allgood;
}
