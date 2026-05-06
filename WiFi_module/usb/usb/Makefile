# Enable A-MSDU support (need FW support)
## Select this if FW is compiled with AMSDU support
CONFIG_BL_SPLIT_TX_BUF ?= n
## Select this TO send AMSDU
CONFIG_BL_AMSDUS_TX ?= n

# Enable BFMER support (need FW support)
CONFIG_BL_BFMER ?= n

# Support of MU-MIMO transmission (need FW support)
ifeq ($(CONFIG_BL_BFMER), y)
CONFIG_BL_MUMIMO_TX ?= n
else
CONFIG_BL_MUMIMO_TX = n
endif

# Enable handling of radar event
CONFIG_BL_RADAR ?= n

# Enable HW queue for Broadcast/Multicast traffic (need FW support)
CONFIG_BL_BCMC ?= y

# Enable Monitor+Data interface support (need FW support)
CONFIG_BL_MON_DATA ?= n

# extra DEBUG config
CONFIG_BL_SW_PROFILING ?= n

CONFIG_BL_BUS_LOOPBACK ?= n

obj-m := bl_usb_drv.o
bl_usb_drv-y := ../bl_msg_tx.o        \
              ../bl_msg_rx.o          \
              ../bl_utils.o           \
              ../bl_ipc.o             \
              ../bl_cmds.o            \
              ../bl_cfgfile.o         \
              ../bl_strs.o            \
              ../bl_rx.o              \
              ../bl_tx.o              \
              ../bl_txq.o             \
              ../bl_main.o            \
              ../bl_mod_params.o      \
              bl_usb.o                \
              ../bl_platform.o	      \
              ../bl_ipc_host.o        \
              ../bl_tdls.o	          \
              ../bl_hal_desc.o        \
              ../bl_irqs.o            \
              ../bl_iwpriv.o          \
              ../bl_bootrom.o
			  
bl_usb_drv-$(CONFIG_MESH)           	+= ../bl_mesh.o			  
bl_usb_drv-$(CONFIG_BL_RADAR)       	+= ../bl_radar.o
bl_usb_drv-$(CONFIG_BL_DEBUGFS)         += ../bl_debugfs.o
bl_usb_drv-$(CONFIG_BL_DEBUGFS)         += ../bl_fw_dump.o
bl_usb_drv-$(CONFIG_BL_DEBUGFS)         += ../bl_fw_trace.o
bl_usb_drv-$(CONFIG_BL_BFMER)       	+= ../bl_bfmer.o
bl_usb_drv-$(CONFIG_BL_MUMIMO_TX)   	+= ../bl_mu_group.o
bl_usb_drv-$(CONFIG_BL_BUS_LOOPBACK)    += ../bl_loopback.o

ifeq ($(CONFIG_BL_DNLD_FWBIN), n)               
    bl_usb_drv-y += ../bl_fwbin.o
ifeq ($(CONFIG_BL_MP), y)
    bl_usb_drv-y += ../bl_mpfwbin.o
endif
endif

ifeq ($(CONFIG_BL_BTUSB),y)
bl_usb_drv-y         += ../btusb/bl_btusb.o
endif

bl_usb_drv-y += ../softmac/softmac.o 			\
         ../softmac/umac/me/me.o 				\
         ../softmac/umac/me/me_mgmtframe.o 		\
         ../softmac/umac/me/me_utils.o 			\
         ../softmac/umac/scanu/scanu.o 			\
         ../softmac/umac/scanu/scanu_task.o 	\
         ../softmac/umac/sm/sm.o 			    \
         ../softmac/umac/sm/sm_task.o 			\
         ../softmac/umac/apm/apm.o 			    \
         ../softmac/umac/apm/apm_task.o 		\
         ../softmac/umac/rm/rm.o 			    \
         ../softmac/umac/rm/rm_task.o 			\
         ../softmac/modules/common/co_list.o 	\
         ../softmac/modules/common/co_math.o 	\
         ../softmac/modules/ke/ke_env.o 		\
         ../softmac/modules/ke/ke_event.o 		\
         ../softmac/modules/ke/ke_mem.o 		\
         ../softmac/modules/ke/ke_msg.o 		\
         ../softmac/modules/ke/ke_queue.o 		\
         ../softmac/modules/ke/ke_task.o 		\
         ../softmac/modules/ke/ke_timer.o 		\
         ../softmac/lmac/txl_frame.o 			\
         ../softmac/modules/mac/mac.o 			\
         ../softmac/modules/mac/mac_ie.o

ccflags-y := -DCONFIG_BL_FULLMAC
ccflags-y += -DCONFIG_BL_USB

ccflags-y += -I$(src)
ccflags-y += -I$(src)/..

ccflags-y += -I$(src)/../softmac
ccflags-y += -I$(src)/../softmac/lmac
ccflags-y += -I$(src)/../softmac/umac/me
ccflags-y += -I$(src)/../softmac/umac/rxu
ccflags-y += -I$(src)/../softmac/umac/scanu
ccflags-y += -I$(src)/../softmac/umac/sm
ccflags-y += -I$(src)/../softmac/umac/apm
ccflags-y += -I$(src)/../softmac/umac/rm
ccflags-y += -I$(src)/../softmac/modules/common
ccflags-y += -I$(src)/../softmac/modules/ke
ccflags-y += -I$(src)/../softmac/modules/mac

ccflags-$(CONFIG_BL_RADAR) += -DCONFIG_BL_RADAR
ccflags-$(CONFIG_BL_MON_DATA) += -DCONFIG_BL_MON_DATA
ccflags-$(CONFIG_BL_BFMER) += -DCONFIG_BL_BFMER
ccflags-$(CONFIG_BL_SPLIT_TX_BUF) += -DCONFIG_BL_SPLIT_TX_BUF
ifeq ($(CONFIG_BL_SPLIT_TX_BUF), y)
ccflags-$(CONFIG_BL_AMSDUS_TX) += -DCONFIG_BL_AMSDUS_TX
endif
ccflags-$(CONFIG_BL_DBG) += -DCONFIG_BL_DBG
ccflags-$(CONFIG_BL_DYN_DBG) += -DCONFIG_BL_DYN_DBG
ccflags-$(CONFIG_BL_SW_PROFILING) += -DCONFIG_BL_SW_PROFILING
ccflags-$(CONFIG_BL_MUMIMO_TX) += -DCONFIG_BL_MUMIMO_TX
ccflags-$(CONFIG_BL_BUS_LOOPBACK) += -DBL_BUS_LOOPBACK

ifeq ($(CONFIG_BL_MUMIMO_TX), y)
ccflags-y += -DCONFIG_USER_MAX=2
else
ccflags-y += -DCONFIG_USER_MAX=1
endif

ifeq ($(CONFIG_BL_BCMC), y)
ccflags-y += -DNX_TXQ_CNT=5
else
ccflags-y += -DNX_TXQ_CNT=4
endif

ifeq ($(CONFIG_BL_BTUSB),y)
ccflags-y += -DCONFIG_BL_BTUSB
endif

ccflags-y += -DCONFIG_CMD_USB_EP

# For old kernel (<=3.19)
ifeq ($(shell test $(VERSION) -lt 4 -a "$(CONFIG_VENDOR_BL)" = y ; echo $$?),0)
ccflags-y += -DCONFIG_VENDOR_BL_VHT_NO80
endif
ccflags-y += -Wimplicit-fallthrough=0
ccflags-y += -Wno-discarded-qualifiers 
#ccflags-y += -Wno-pointer-to-int-cast

#
#clean-files := bl_version_gen.h
