# Verification performed during decoding #

  * That the prototype nesting depth is at most `LUAI_MAXCCALLS` (as otherwise, loading of the chunk can smash the C stack).
  * That boolean constants in the constant table are `0` or `1`, and not any other non-zero values (as otherwise, booleans can be constructed whose value is neither `true` nor `false`).

# Per-instruction verification performed once #

  * That _test_ instructions are proceeded by a `JMP` instruction.
  * That boolean constants in `LOADBOOL` instructions are `0` or `1`.
  * That all referenced registers are less than the prototype's `maxstack`.
  * That all referenced constants exist in the prototype's constant table.
  * That all referenced upvalues are less than the prototype's `numupvalues`.
  * That all referenced prototypes exist in the prototype's sub-prototype list.
  * That `VARARG` instructions only occur in prototypes marked as taking a variable argument list.
  * That `SETLIST` and `LOADK` instructions are proceeded by an `EXTRAARG` instruction if their index field is `0`.
  * That control flow doesn't leave the instruction list after execution of the instruction.

# Per-instruction verification performed with tracing information #
  * That whenever a register is read from, or used for anything other than being assigned to (including being made into an upvalue), the value in the register was previously assigned to from within the function (or when the register is an upvalue, set by an upvalue assignment elsewhere). In particular, this means that the contents of registers left behind by other function calls cannot be obtained or used.
  * That when a call is made, none of the registers containing function arguments are open upvalues (as otherwise, the values in those registers could be changed after the called function validates them, thus allowing argument validation to be bypassed).
  * That when a call is made, the register containing the function itself is not an open upvalue (as otherwise, all references to the currently running function could be removed, potentially leading to the currently running function being garbage collected).
  * That when an instruction uses a variable range of registers (via _top_), that range could not have a negative size, and that _top_ was set by the previous instruction.
  * That the loop control registers used by `FORLOOP` all hold number values (as otherwise, values which are not numbers can be cast to numbers, allowing for information leakage).
  * That the register used by `SETLIST` which should contain a table value does infact hold a table value (as otherwise, values which are not tables can be reinterpreted as tables).