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

assert(_VERSION == "Lua 5.2", "Lua 5.2 is required")
local bv = require "bytecodeverify"
local asm = require "assemble"

local tests
local settestenv
do
  local _ENV = _ENV
  settestenv = function(...) _ENV = ... end
  tests = {"Bytecode verifier tests",
    {"Code generator acceptance",
      {"Minimal", function()
        assertTrue(bv.verify(string.dump(function()end)))
      end},
      {"Local assignments", function()
        assertTrue(bv.verify(string.dump(function()
          local a, b = 0, 1
          a, b = b, a
        end)))
      end},
      {"Assignments", function()
        assertTrue(bv.verify(string.dump(function()
          a, b = 0, 1
          a, b = b, a
        end)))
      end},
      --[[
      {"Constants", function()
        local t = {"local x"}
        for i = 2, 2^18 + 2 do
          t[i] = ";x="..i
        end
        assertTrue(bv.verify(string.dump(assert(loadstring(table.concat(t))))))
      end},
      --]]
      {"Arguments", function()
        assertTrue(bv.verify(string.dump(function(a, b)
          return b, a
        end)))
      end},
      {"Calls",
        {"Function call", function()
          assertTrue(bv.verify(string.dump(function()
            os.execute("rm -rf /")
          end)))
        end},
        {"Tail call", function()
          assertTrue(bv.verify(string.dump(function()
            return collectgarbage()
          end)))
        end},
        {"Method call", function()
          assertTrue(bv.verify(string.dump(function()
            x:y()
          end)))
        end},
      },
      {"Branching",
        {"Simple if", function()
          assertTrue(bv.verify(string.dump(function()
            local x
            if y then
              x = 0
            else
              x = 1
            end
            return x
          end)))
        end},
        {"Complex if", function()
          assertTrue(bv.verify(string.dump(function()
            local z
            if (x and y) or not (x or y) then
              z = 0
            else
              z = 1
            end
            return z
          end)))
        end},
        {"Comparison assignment", function()
          assertTrue(bv.verify(string.dump(function(y, z)
            x = y < z
          end)))
        end},
        {"Logical assignment", function()
          assertTrue(bv.verify(string.dump(function(y, z)
            x = y or z
          end)))
        end},
      },
      {"Loops",
        {"While", function()
          assertTrue(bv.verify(string.dump(function()
            local x = 0
            while true do x = x + 1 end
          end)))
        end},
        {"Numeric for, constant control", function()
          assertTrue(bv.verify(string.dump(function()
            for i = 1, 10 do print(i) end
          end)))
        end},
        {"Numeric for, variable control", function()
          assertTrue(bv.verify(string.dump(function()
            for i = init, limit, step do end
          end)))
        end},
        {"Generic for", function()
          assertTrue(bv.verify(string.dump(function()
            for k, v in pairs(_G) do print(k, v) end
          end)))
        end},
      },
      {"Tables",
        {"Minimal", function()
          assertTrue(bv.verify(string.dump(function()
            local t = {}
          end)))
        end},
        {"Hash part", function()
          assertTrue(bv.verify(string.dump(function()
            local t = {x = 0, y = 1, z = 2}
          end)))
        end},
        {"Array part", function()
          assertTrue(bv.verify(string.dump(function()
            local t = {0, 1, 2}
          end)))
        end},
      },
      {"Closures",
        {"Minimal", function()
          assertTrue(bv.verify(string.dump(function()
            local function x() end
          end)))
        end},
        {"Open upvalues", function()
          assertTrue(bv.verify(string.dump(function()
            local x, up
            x = function() up = up + 1 return up end
            x()
          end)))
        end},
        {"Closed upvalues", function()
          assertTrue(bv.verify(string.dump(function()
            local x
            do
              local up
              x = function() up = up + 1 return up end
            end
            x()
          end)))
        end},
        {"Recursive", function()
          assertTrue(bv.verify(string.dump(function()
            local function f() return f() end
            return f()
          end)))
        end},
        {"Methods", function()
          assertTrue(bv.verify(string.dump(function()
            function x:y() end
          end)))
        end},
      },
      {"Varargs",
        {"Identity function", function()
          assertTrue(bv.verify(string.dump(function(...)
            return ...
          end)))
        end},
        {"Function chaining", function()
          assertTrue(bv.verify(string.dump(function()
            f(x, g(h()))
          end)))
        end},
        {"Assignment", function()
          assertTrue(bv.verify(string.dump(function(...)
            local x, y, z = ...
          end)))
        end},
        {"Into table", function()
          assertTrue(bv.verify(string.dump(function(...)
            local t = {...}
          end)))
        end},
      },
      {"Test script", function()
        assertTrue(bv.verify(string.dump(assert(loadfile"test.lua"))))
      end},
      {"Assembler script", function()
        assertTrue(bv.verify(string.dump(assert(loadfile"assemble.lua"))))
      end},
    },
    {"Assembler self-test",
      {"Minimal", function()
        assertTrue(bv.verify(asm.assemble[[
          return 0 1
        ]]))
      end},
      {"Registers and constants", function()
        assertTrue(bv.verify(asm.assemble[[
          .stack 4
          .constant aHello "Hello"
          loadk 0 aHello
          .constant f42 42
          loadk 1 f42
          move 2 0
          move 3 1
          return 0 5
        ]]))
      end},
    },
    {"Identification of malformed bytecode",
      {"Empty string", function()
        assertMalformed(bv.verify(""))
      end},
      {"Extra data at end of stream", function()
        assertMalformed(bv.verify(string.dump(function()end) .. "\0"))
      end},
      {"Uncompiled code", function()
        assertMalformed(bv.verify("print[[Hello world from Lua 5.2]]"))
      end},
      {"Infinitely nested prototypes", function()
        local body = [[
          .r tmp 0
          closure tmp A
          return tmp 2
          
          .proto A
          .r tmp 0
          closure tmp X
          return tmp 2
          
          .proto B
          return 0 1
        ]]
        assertTrue(bv.verify(asm.assemble(body:gsub("X", "B"))))
        assertMalformed(bv.verify(asm.assemble(body:gsub("X", "A"))))
      end},
    },
    {"Malicious bytecode",
      {"Cast to number", function()
        -- This piece of malicious bytecode allows for information leakage by
        -- changing the type field of a TValue to LUA_TNUMBER. The verifier
        -- should only allow the "forloop" instruction when it can guarantee
        -- that numbers are involved.
        local body = [[
          .stack 4
          .k huge math.huge
          .k zero 0
          loadk 0 magic
          loadk 1 huge
          loadk 2 zero
          forloop 0 0
          return 0 2
        ]]
        assertTrue(bv.verify(asm.assemble(".k magic 0\n" .. body)))
        assertMalicious(bv.verify(asm.assemble(".k magic ''\n" .. body)))
      end},
      {"Reinterpret as table", function()
        -- This bytecode is malicious as it interprets register 0 as being a
        -- table, even when it isn't. This can lead to arbitrary memory writes,
        -- provided that specially crafted values are reinterpreted as tables.
        local body = [[
          .params 2
          .stack 2
          setlist 0 1 1
          return 0 1
        ]]
        assertTrue(bv.verify(asm.assemble("newtable 0 1 1\n" .. body)))
        assertMalicious(bv.verify(asm.assemble(body)))
      end},
      {"Reinterpret as table, with jumping", function()
        -- This bytecode is malicious for the same reason as above. This time,
        -- it attempts to trick the verifier by jumping back to a setlist which
        -- was safe the first time around.
        local body = [[
          .params 2
          .r argT 0
          .r argV 1
          .r T 2
          .r V 3
          newtable T 0 0
          loadnil V V
          reentry:
          setlist T 1 1
          move V argV
          move T X
          jmp 0 reentry
        ]]
        assertTrue(bv.verify(asm.assemble(body:gsub("X", "T"))))
        assertMalicious(bv.verify(asm.assemble(body:gsub("X", "argT"))))
      end},
      {"Original lowcall", function()
        -- This bytecode is malicious as it calls a function, and then reads
        -- from the registers used by the function, potentially allowing the
        -- contents of registers to leak out of a function call.
        local body = [[
          .params 1
          .vararg
          .stack 20
          loadnil 1 19
          move call_base 0
          vararg call_args 0
          call call_base 0 1
          return 0 11
        ]]
        assertTrue(bv.verify(asm.assemble(body .. ".r call_base 10 \n .r call_args 11")))
        assertMalicious(bv.verify(asm.assemble(body .. ".r call_base 0 \n .r call_args 1")))
      end},
      {"Upvalue lowcall", function()
        -- This bytecode is malicious as it allows for open upvalues to be
        -- created over registers of an arbitrary function, thus allowing for
        -- parameter validation to be bypassed.
        local body = [[
          .params 2
          .r callback 0
          .r lowcall 1
          .vararg
          .r up1 2
          .r up2 3
          .r up3 4
          .r fn 5
          .r arg 6
          loadnil up1 up3
          move fn callback
          closure arg set_upvals
          call fn 2 1
          close close_base
          vararg up1 0
          call lowcall 0 1
          return 0 1
          
          .proto set_upvals
          .up up1
          .up up2
          .up up3
          .stack 3
          .params 3
          setupval 0 up1
          setupval 1 up2
          setupval 2 up3
          return 0 1
        ]]
        assertTrue(bv.verify(asm.assemble(".r close_base 2\n" .. body)))
        assertMalicious(bv.verify(asm.assemble(".r close_base 5\n" .. body)))
      end},
    },
    {"Exotic bytecode acceptance",
      {"Ignore unexecutable code", function()
        -- The "move 3 4" instruction is invalid, but cannot ever get executed,
        -- so should still be allowed.
        assertTrue(bv.verify(asm.assemble[[
          jmp 0 theend
          move 3 4
          theend:
          return 0 1
        ]]))
      end},
      {"Infinite jmp loop", function()
        -- The old Lua 5.1 verifier required that the last instruction of a
        -- prototype be a "return" instruction, and everything generated by the
        -- code generator will have this, but the following code is safe even
        -- without a single "return" instruction.
        assertTrue(bv.verify(asm.assemble"infinite: jmp 0 infinite"))
      end},
    },
  }
  --[=[
  for filename in io.popen([[dir /B /S C:\CPP\CorsixTH\CorsixTH\Lua\]]):lines() do
    if filename:sub(-4, -1) == ".lua" then
      tests[#tests + 1] = {filename, function()
        assertTrue(bv.verify(string.dump(assert(loadfile(filename)))))
      end}
    end
  end
  --]=]
end

local num_asserts
local num_pass = 0
local num_neutral = 0
local num_fail = 0
local failmsg

local testenv = setmetatable({
  bv = bv,
  assertTrue = function(x, ...)
    num_asserts = num_asserts + 1
    failmsg = ...
    assert(not not x)
    failmsg = nil
  end,
  assertMalformed = function(status, err)
    num_asserts = num_asserts + 1
    failmsg = err
    assert(err == "unable to load bytecode")
    failmsg = nil
  end,
  assertMalicious = function(status, err)
    num_asserts = num_asserts + 1
    failmsg = err
    assert(err == "verification failed")
    failmsg = nil
  end,
}, {__index = _G})

local function runtests(tests, prefix)
  assert(type(tests[1]) == "string")
  io.write(prefix .. tests[1])
  for i = 2, #tests do
    local test = tests[i]
    if type(test) == "table" then
      io.write("\n")
      runtests(test, prefix .. "  ")
    else
      assert(type(test) == "function" and i == 2 and #tests == 2)
      num_asserts = 0
      failmsg = nil
      settestenv(setmetatable({}, {__index = testenv}))
      local status, err = pcall(test)
      if status then
        if num_asserts > 0 then
          io.write(" -- pass")
          num_pass = num_pass + 1
        else
          io.write(" -- passed vacuously")
          num_neutral = num_neutral + 1
        end
      else
        io.write(" -- FAIL")
        failmsg = failmsg or err
        if failmsg then
          io.write(" (" .. failmsg .. ")")
        end
        num_fail = num_fail + 1
      end
    end
  end
end

runtests(tests, "")
io.write("\n")
print((num_pass + num_neutral + num_fail) .. " tests run, of which:")
print("  " .. num_pass .. " passed")
print("  " .. num_neutral .. " passed vacuously")
print("  " .. num_fail .. " failed")