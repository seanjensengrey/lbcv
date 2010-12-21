#------
# Load configuration
#
include ../config

#------
# Hopefully no need to change anything below this line
#

LBCV_OBJS:= \
  decoder.o \
  interface.o \
  verifier.o \
  opcodes.o

all: $(LBCV_SO)

$(LBCV_SO): $(LBCV_OBJS)
	$(LD) $(LDFLAGS) -o $@ $(LBCV_OBJS)

#------
# List of dependencies
#
decoder.o: decoder.c decoder.h opcodes.h defs.h
interface.o: interface.c decoder.h verifier.h opcodes.h defs.h
verifier.o: verifier.c verifier.h decoder.h opcodes.h defs.h
opcodes.o: opcodes.c opcodes.h

clean:
	rm -f $(LBCV_SO) $(LBCV_OBJS) 

#------
# End of makefile configuration
#
