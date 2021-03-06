--[[ Copyright (c) 2010 Peter Cawley

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
SOFTWARE. ]]

--[[ Utility module for crafting Lua 5.2 bytecode which cannot be generated by
the default code generator. This is done in the form of a crude line-based
assembler, which takes plaintext with at most one virtual machine instruction
or assembler directive per line, and outputs a reader function which returns
successive chunks of bytecode.

Exports a single function called "assemble".

]]

-- Create lookup-table for opcode names to codes
local opcode_name_to_code = {}
for code, name in pairs{[0] = "MOVE", "LOADK", "LOADBOOL", "LOADNIL",
  "GETUPVAL", "GETTABUP", "GETTABLE", "SETTABUP", "SETUPVAL", "SETTABLE",
  "NEWTABLE", "SELF", "ADD", "SUB", "MUL", "DIV", "MOD", "POW", "UNM", "NOT",
  "LEN", "CONCAT", "JMP", "EQ", "LT", "LE", "TEST", "TESTSET", "CALL",
  "TAILCALL", "RETURN", "FORLOOP", "FORPREP", "TFORCALL", "TFORLOOP",
  "SETLIST", "CLOSE", "CLOSURE", "VARARG", "EXTRAARG"}
do
  opcode_name_to_code[name] = code
end

-- Lookup table for opcode modes ("ABC" mode is assumed if not listed)
local opcode_modes = {LOADK = "ABx", JMP = "AsBx", FORLOOP = "AsBx",
  FORPREP = "AsBx", TFORLOOP = "AsBx", CLOSURE = "ABx", EXTRAARG = "Ax"}

--[[ Helper function to construct a function which packs integer values into a
 string.
 @param nbytes The number of bytes which the resulting function will pack
               integers into.
 @param endian The endianness to pack integers as; either "big" or "little".
 @return A function which accepts a positive integer and returns a string whose
         bytes represent the integer. ]]
local function make_ub(nbytes, endian)
  assert(nbytes >= 0)
  assert(endian == "little" or endian == "big")
  --[[ Construct a function like:
    function(n)
      local b1 = n % 0x100; n = (n - b1) / 0x100;
      local b2 = n % 0x100; n = (n - b2) / 0x100;
      return string.char(b1, b2)
    end ]]
  local code = "return function(n)"
  for i = 1, nbytes do
    code = code .. ("local b%i = n %% 0x100; n = (n - b%i) / 0x100; "):format(i, i)
  end
  code = code .. "return string.char("
  local init, limit, step = 1, nbytes, 1
  if endian == "big" then
    init, limit, step = limit, init, -step
  end
  for i = init, limit, step do
    if i ~= init then
      code = code .. ", "
    end
    code = code .. "b" .. i
  end
  code = code .. ") end"
  return assert(loadstring(code))()
end

--[==[ Assemble plaintext into Lua 5.2 bytecode.

The format of accepted code is:
code ::= line {[\r\n]+ line}
line ::= directive | instruction
directive ::= .(constant|k) Name Expression |
              .(proto|prototype) Name |
              .vararg |
              .stack[size] Number |
              .(params|args) Number |
              .r[eg] Name Number |
              .up[value] Name
instruction ::= {label} opcode [operand [operand [operand]]]
label ::= Name:
opcode ::= move | loadk | loadbool  | ... | vararg | extraarg
operand ::= Name | Number

The assembler has a concept of the "current prototype", with instructions and
certain directives affecting the "current prototype". The current prototype
starts as the top-level chunk, and can be changed with the ".prototype"
directive.

The ".constant" directive (shortened to ".k") is used to give a name to a
specific value. The expression following the name is evaluated at compile-time
to obtain the value of the constant. The list of named constants lives outside
of each individual prototype, so constants can be used anywhere as long as they
are defined somewhere.

The ".prototype" directive (shortened to ".proto") is used to start a new
prototype. Subsequent prototype-specific directives will affect the new
prototype, and likewise instructions will be appended to the new prototype.
The name given in the directive can be used in "closure" instructions in other
prototypes to instantiate the prototype.

The ".vararg" directive marks the current prototype as accepting a variable
length argument list.

The ".stacksize" directive (shortened to ".stack") specifies the number of
registers which the current prototype needs. If the given number is N, then
registers 0 through N - 1 (inclusive) will be available for use.

The ".params" directive (shortened to ".args") specifies the number of fixed
arguments which the current prototype expects. If the given number is N, then
registers 0 through N - 1 (inclusive) will be initialised with the passed
arguments (or nil) when an instance of the prototype is called.

The ".reg" directive (shortened to ".r") assigns a name to a register for the
entirity of the current prototype, and ensures that the stack size is set high
enough for the register to exist.

The ".upvalue" directive (shortened to ".up") creates a new upvalue in the
current prototype, which refers to a register or upvalue of the enclosing
prototype with the same name. The specified name can then be used in any
instruction which expects an upvalue.

Labels are of the form "Name:", and assign a name to the next instruction. This
name can then be used anywhere in the current prototype to mean the offset
between the instruction it is used in, and the instruction it labels.

]==]
local function assemble(code)
  ----- Parse text into structures -----
  local constants = {} -- Map of constant name to constant value
  local proto = { -- The current prototype
    code = {}, -- Array of instructions, each instruction being a table of form
               -- {op = N, a = N, b = N, c = N}, where N is a name or number.
    upvalues = {}, -- Array of upvalue names, and map of upvalue name to index
    labels = {}, -- Map of label name to absolute program counter position
    regs = {}, -- Map of register name to register index
  }
  local prototypes = {_main = proto} -- Map of prototype name to prototype
  local labels = {} -- The set of labels to be applied to the next instruction
  for line in code:gmatch"[^\r\n]+" do repeat
    -- Check if the line contains a directive, and handle it if it does
    local directive, extra = line:match"^%s*%.([a-zA-Z_]+)(.*)$"
    if directive then
      local ldirective = directive:lower()
      if ldirective == "constant" or ldirective == "k" then
        local name, extra = extra:match"^%s*([a-zA-Z_][a-zA-Z0-9_]*)(.*)$"
        constants[name or error("Expected name after " .. directive)] = assert(loadstring("return " .. extra), "Expected value after " .. directive .. " " .. name)()
      elseif ldirective == "proto" or ldirective == "prototype" then
        proto = {code = {}, upvalues = {}, labels = {}, regs = {}}
        prototypes[extra:match"[a-zA-Z_][a-zA-Z0-9_]*" or error("Expected name after " .. directive)] = proto
      elseif ldirective == "up" or ldirective == "upvalue" then
       local name = extra:match"[a-zA-Z_][a-zA-Z0-9_]*" or error("Expected name after " .. directive)
       proto.upvalues[name] = #proto.upvalues
       proto.upvalues[#proto.upvalues + 1] = name
      elseif ldirective == "vararg" then
        proto.vararg = true
      elseif ldirective == "stack" or ldirective == "stacksize" then
        proto.numregs = tonumber(extra) or error("Expected number after " .. directive)
      elseif ldirective == "params" or ldirective == "args" then
        proto.numparams = tonumber(extra) or error("Expected number after " .. directive)
      elseif ldirective == "reg" or ldirective == "r" then
        local name, extra = extra:match"^%s*([a-zA-Z_][a-zA-Z0-9_]*)(.*)$"
        proto.regs[name or error("Expected name after " .. directive)] = tonumber(extra) or error("Expected number after " .. directive)
        proto.numregs = math.max(proto.numregs or 0, tonumber(extra) + 1)
      else
        error("Unrecognised assembler directive: " .. directive)
      end
      break -- continue (to next line)
    end
    
    -- Strip any labels off the front of the line
    while true do
      local label, extra = line:match"^%s*([a-zA-Z_][a-zA-Z0-9_]*):(.*)$"
      if not label then
        break
      end
      labels[label] = true
      line = extra
    end
    
    -- Check if the line contains an instruction
    local op, a, b, c = line:match"([a-zA-Z_]+)%s*(%S*)%s*(%S*)%s*(%S*)"
    if op then
      local uop = op:upper()
      if not opcode_name_to_code[uop] then
        error("Unrecognised opcode: " .. op)
      end
      for label in pairs(labels) do
        proto.labels[label] = #proto.code
      end
      proto.code[#proto.code + 1] = {
        op = uop,
        a = tonumber(a) or a,
        b = tonumber(b) or b,
        c = tonumber(c) or c
      }
      if next(labels) then
        labels = {}
      end
    end
  until true end
  
  ----- Link (resolve names to numbers, populate constant tables, etc.) -----
  for name, proto in pairs(prototypes) do
    proto.prototypes = {}
    proto.constants = {}
    local usedprototypes = {}
    local usedconstants = {}
    for pc, ins in pairs(proto.code) do
      local op = ins.op
      for i, arg in pairs(ins) do
        if i ~= "op" and type(arg) ~= "number" then
          -- Simple name -> number replacements
          if proto.regs[arg] then
            arg = proto.regs[arg]
          elseif proto.upvalues[arg] then
            arg = proto.upvalues[arg]
          -- Name -> number replacement with PC offset
          elseif proto.labels[arg] then
            arg = proto.labels[arg] - pc
          -- Name -> cached table index replacements
          elseif prototypes[arg] then
            if not usedprototypes[arg] then
              usedprototypes[arg] = #proto.prototypes
              proto.prototypes[#proto.prototypes + 1] = prototypes[arg]
            end
            arg = usedprototypes[arg]
          elseif constants[arg] then
            if not usedconstants[arg] then
              usedconstants[arg] = #proto.constants
              proto.constants[#proto.constants + 1] = constants[arg]
            end
            arg = usedconstants[arg]
            if op == "LOADK" then
              -- LoadK offsets constant indicies by 1 to allow 0 to be used to
              -- mean "Use ExtraArg".
              arg = arg + 1
            elseif op ~= "EXTRAARG" then
              -- Constants elsewhere than LoadK and ExtraArg are only found in
              -- RK fields, so they need an offset to make them K rather than R
              arg = arg + 256
            end
          -- Ommitted operands are treated as zero
          elseif arg == "" then
            arg = 0
          else
            error("Unable to resolve symbol '" .. arg .. "'")
          end
          ins[i] = arg
        end
      end
    end
  end
  
  ----- Compile (spit out bytecode) -----
  local header = string.dump(function()end)
  local endian = header:byte(7) == 0 and "big" or "little"
  local sizeof_int = header:byte(8)
  local sizeof_sizet = header:byte(9)
  local sizeof_instruction = header:byte(10)
  header = nil -- Is no longer used
  local yield = coroutine.yield
  local ub_int = make_ub(sizeof_int, endian)
  local function int(value) -- Helper to write an int
    yield(ub_int(value))
  end
  local ub_instruction = make_ub(sizeof_instruction, endian)
  local function instruction(value) -- Helper to write an instruction
    yield(ub_instruction(value))
  end
  local ub_sizet = make_ub(sizeof_sizet, endian)
  local function str(s) -- Helper to write a string
    if s then
      yield(ub_sizet(#s + 1))
      if s ~= "" then
        yield(s)
      end
      yield "\0"
    else
      yield(ub_sizet(0))
    end
  end
  local ub4 = make_ub(4, endian)
  local function num(n) -- Helper to write an IEEE754 double-precision float
    local sign = 0
    -- Ensure that n is strictly positive
    if n == 0 then
      yield "\0\0\0\0\0\0\0\0"
      return
    elseif n < 0 then
      sign = 1
      n = -n
    end
    -- Split into mantissa and exponent
    local m, e = math.frexp(n)
    m = (m - 0.5) * 2^53 -- Discard most significant bit, make into integer
    e = e + 1022 -- Apply exponent bias, and offset by 1 for removing MSB
    local lowm = m % 2^32
    m = (m - lowm) / 2^32
    local low = ub4(lowm)
    local high = ub4(sign * 2^31 + e * 2^20 + m)
    if endian == "big" then
      yield(high)
      yield(low)
    else
      yield(low)
      yield(high)
    end
  end
  return coroutine.wrap(function()
    -- Header
    yield "\27Lua\x52\0"
    yield(string.char(endian == "little" and 1 or 0, sizeof_int, sizeof_sizet, sizeof_instruction))
    yield "\8\0\x19\x93\r\n\x1a\n"
    -- Helper for an individual prototype
    local function compile(proto, parent)
      -- Line, last line
      int(0)
      int(0)
      -- Stack and parameter info
      yield(string.char(proto.numparams or 0, proto.vararg and 1 or 0, proto.numregs or 0))
      -- Instruction array
      int(#proto.code)
      for _, ins in ipairs(proto.code) do
        local mode = opcode_modes[ins.op]
        local op = opcode_name_to_code[ins.op]
        local encoded = op + ins.a * 2^6
        if not mode then
          encoded = encoded + ins.c * 2^14 + ins.b * 2^23
        elseif mode == "AsBx" then
          encoded = encoded + (ins.b + 131071) * 2^14
        elseif mode == "ABx" then
          encoded = encoded + ins.b * 2^14
        elseif mode ~= "Ax" then
          error("Unknown encoding mode " .. mode)
        end
        instruction(encoded)
      end
      -- Constant table (exlcuding prototypes)
      int(#proto.constants)
      for _, k in ipairs(proto.constants) do
        local t = type(k)
        if t == "boolean" then
          yield(k and "\1\1" or "\1\0")
        elseif t == "nil" then
          yield "\0"
        elseif t == "number" then
          yield "\3"
          num(k)
        elseif t == "string" then
          yield "\4"
          str(k)
        else
          error("Invalid constant type: " .. t)
        end
      end
      -- Prototypes
      int(#proto.prototypes)
      for _, p in ipairs(proto.prototypes) do
        compile(p, proto)
      end
      -- Upvalues
      int(#proto.upvalues)
      for _, upvalue in ipairs(proto.upvalues) do
        if not parent then
          yield "\0\0"
        elseif parent.regs[upvalue] then
          yield(string.char(1, parent.regs[upvalue]))
        elseif parent.upvalues[upvalue] then
          yield(string.char(0, parent.upvalues[upvalue]))
        else
          error("Unresolved upvalue: " .. upvalue)
        end
      end
      -- Debug information
      str(nil)
      int(0)
      int(0)
      int(0)
    end
    -- Recurse from top-level prototype
    compile(prototypes._main, nil)
    -- Return nothing more
    while true do yield "" end
  end)
end

return {
  assemble = assemble
}
