A module for Lua 5.2 which verifies that pre-compiled code (bytecode) is non-malicious, and hence can be executed without fear of it taking advantage of implementation details of the Lua 5.2 virtual machine.

For the purposes of this module, the following code is **not** classified as malicious:
```
os.execute("rm -rf /")
```
This is because the above code fragment is perfectly valid Lua code, and the behaviour is dependant upon the functions exposed to it (even though with the default set of exposed functions, it will do very bad things). On the other hand, the following pseudo-bytecode **is** classified as malicious:
```
.params 2
.stack 2
setlist 0 1 1
return 0 1
```
This is because the virtual machine's `setlist` instruction assumes that the specified register is a table without checking it, meaning that if a non-table value is supplied, fun things can happen, including arbitrary memory writes.