TOP=..

include $(TOP)/configure/CONFIG

#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#==================================================
# Build an IOC support library

LIBRARY_IOC += sis8300DigiComm
sis8300DigiComm_SRCS_DEFAULT = sis8300DigiComm-stub.c
sis8300DigiComm_SRCS_Linux = sis8300DigiComm.c sis8300Digi.c msgQ.c devSis8300Stringin.c
DBD                = sis8300DigiComm.dbd
PROD_IOC    += c109

DB += sis8300UsrData.db

c109_SRCS=c109.c sis8300Digi.c
#c109_SYS_LIBS_Linux+=rt

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
