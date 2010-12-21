#------
# Load configuration
#
include config

#------
# Hopefully no need to change anything below this line
#

all clean:
	cd src; $(MAKE) $@

test:	dummy
	cd test; lua test.lua

dummy:

#------
# Install lbcv according to recommendation
#
install: all
	cd src; $(INSTALL_EXEC) $(LBCV_SO) $(INSTALL_TOP_LIB)/lbcv.$(EXT)

#------
# End of makefile
#
