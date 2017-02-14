TOP=..

include $(TOP)/configure/CONFIG

#=====================================================================
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#======================================================================

# =====================================================================
# Interface to Struck SIS8300 for mTCA 0.4 : linux kernel module
USR_INCLUDES += -I$(KERNEL_MODULE_SIS8300DRV_INCLUDE)
# ====================================================================

# ====================================================================
# Export interface for other modules to use
# ====================================================================
INC               += sis8300Digi.h
# =====================================================================

#======================================================================
# Build an IOC support library
# ======================================================================
LIBRARY_IOC_Linux += sis8300Digi
sis8300Digi_SRCS   = sis8300Digi.c ratapp.c
PROD_IOC_Linux    += c109

c109_SRCS=c109.c
c109_LIBS=sis8300Digi
#c109_SYS_LIBS_Linux+=rt

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
