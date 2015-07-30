# Loading lbcv #

```
local lbcv = require "lbcv"
```

# API #

## `lbcv.load(ld [, source [, mode]])` ##

A drop-in replacement for Lua 5.2's `load` function, which is extended with bytecode verification. When `mode` contains `"b"` (which the default value for `mode` does), and a binary chunk is given by `ld`, then this function can return the following error messages in addition to those given by `load`:
  * `"unknown decoding error"` - An unknown error occurred in lbcv's bytecode decoding process.
  * `"insufficient memory"` - Insufficient memory was available for lbcv's bytecode decoding process.
  * `"unable to load bytecode"` - lbcv classified the binary chunk as containing invalid bytecode. This particular value should not be seen, as Lua's loader should classify binary chunks as invalid sooner than lbcv does.
  * `"verification failed"` - lbcv decided that the bytecode in the binary chunk might be malicious.

## `lbcv.verify(ld)` ##

Classify a binary chunk as potentially-malicious or certainly-not-malicious, without loading it.

If `ld` is a function, calls it repeatedly to get the chunk pieces. Each call to `ld` must return a string that concatenates with previous results. A return of an empty string, **nil**, or no value signals the end of the chunk.

If `ld` is a string, the chunk is this string.

If the given chunk is a binary chunk whose bytecode is certainly not malicious, then the return value is **true**. Otherwise, **nil** plus an error message is returned, where the error message is one of:
  * `"unknown decoding error"` - An unknown error occurred in lbcv's bytecode decoding process.
  * `"insufficient memory"` - Insufficient memory was available for lbcv's bytecode decoding process.
  * `"unable to load bytecode"` - lbcv classified the chunk as not containing bytecode, or containing invalid bytecode.
  * `"verification failed"` - lbcv decided that the bytecode in the binary chunk might be malicious.