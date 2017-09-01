ifndef QCONFIG
QCONFIG=qconfig.mk
endif
include $(QCONFIG)

BIND = $(strip $(FBIND) $(CBIND))
FBBIND = $(filter io-blk, $(FBIND))
FSBIND = $(filter-out io-blk, $(FBIND))

ifndef NAME
NAME=devb-$(SECTION)
EXTRA_SILENT_VARIANTS+=$(SECTION)
endif

SERVICES_ROOT=$(PRODUCT_ROOT)/../services

EXTRA_INCVPATH += $(SERVICES_ROOT)/blk
EXTRA_INCVPATH += $(PROJECT_ROOT)/include
EXTRA_INCVPATH += $(PROJECT_ROOT)/$(SECTION)/public
USEFILE=$(PROJECT_ROOT)/$(SECTION)/$(NAME).use
INSTALLDIR=sbin
EXCLUDE_OBJS = dl.o
CCFLAGS += -D__DEVB_NAME__=$(BUILDNAME)
CCFLAGS += -DCAM_PTHREAD_SETNAME
		
ifneq ($(BIND),)
#
# Static link some stuff into devb.*
#
DEFLIB_VARIANT = $(patsubst a.,a,$(subst $(space),.,a $(filter wcc be le,$(VARIANTS))))

LIB_VARIANT = $(firstword $(subst ./,,$(dir $(bind))) $(DEFLIB_VARIANT))

CCFLAGS += -Ddlopen=bound_dlopen -Ddlsym=bound_dlsym -Ddlclose=bound_dlclose -Ddlerror=bound_dlerror
CCFLAGS += $(foreach var,$(FBIND), -DDLL_$(subst -,_,$(notdir $(var)))) $(foreach var,$(CBIND), -DDLL_cam_$(notdir $(var))) -DDLL_cam
EXTRA_LIBVPATH += $(foreach bind,$(FBBIND),$(SERVICES_ROOT)/blk/$(notdir $(bind))/$(CPU)/$(LIB_VARIANT))
EXTRA_LIBVPATH += $(foreach bind,$(FSBIND),$(SERVICES_ROOT)/blk/fs/$(notdir $(bind))/$(CPU)/$(LIB_VARIANT))
EXTRA_LIBVPATH += $(foreach bind,$(CBIND),$(PROJECT_ROOT)/cam/drivers/$(notdir $(bind))/$(CPU)/$(LIB_VARIANT))
EXTRA_OBJS += dl.o

LIBS += $(notdir $(FBBIND)) $(addprefix fs-, $(notdir $(FSBIND))) $(addprefix cam-,$(notdir $(CBIND))) 
ifneq ($(CBIND),)
EXTRA_LIBVPATH += $(PROJECT_ROOT)/cam/cam/$(CPU)/$(DEFLIB_VARIANT)
EXTRA_LIBVPATH += $(SERVICES_ROOT)/../lib/usbdi/$(CPU)/$(DEFLIB_VARIANT)
LIBS += cam cache
endif
endif
ifeq ($(CBIND),)
LDOPTS += -lcam -M
endif
LIBS += drvr cache

ifeq ($(BIND),)
EXTRA_LIBVPATH+=$(PROJECT_ROOT)/cam/cam/$(subst x86/so.,x86/so,$(CPU)/so.$(filter le be,$(subst ., ,$(VARIANTS))))
EXTRA_LIBVPATH+=$(SERVICES_ROOT)/../lib/usbdi/$(subst x86/so.,x86/so,$(CPU)/so.$(filter le be,$(subst ., ,$(VARIANTS))))
endif

define PINFO
PINFO DESCRIPTION=
endef

include $(MKFILES_ROOT)/qmacros.mk
-include $(SECTION_ROOT)/pinfo.mk

-include $(PROJECT_ROOT)/roots.mk
#####AUTO-GENERATED by packaging script... do not checkin#####
   INSTALL_ROOT_nto = $(PROJECT_ROOT)/../../../install
   USE_INSTALL_ROOT=1
##############################################################

include $(MKFILES_ROOT)/qtargets.mk

#
# So that it recompiles if the BIND list is changed.
#
dl.o : Makefile ../../../common.mk

nda:
	$(foreach f, $(NDAS), $(CP_HOST) $(patsubst %.o-nda, %.o, $(f)) $(f);)
