ifndef QCONFIG
QCONFIG=qconfig.mk
endif
include $(QCONFIG)

include $(MKFILES_ROOT)/qmacros.mk
-include $(PROJECT_ROOT)/roots.mk
#####AUTO-GENERATED by packaging script... do not checkin#####
   INSTALL_ROOT_nto = $(PROJECT_ROOT)/../../../../install
   USE_INSTALL_ROOT=1
##############################################################

define PINFO
PINFO DESCRIPTION=AM335X BoardId Driver
endef

NAME := bdid-$(NAME)
USEFILE = $(PROJECT_ROOT)/Usemsg
INSTALLDIR = sbin
LIBS = i2c-master drvrS

include $(MKFILES_ROOT)/qtargets.mk
