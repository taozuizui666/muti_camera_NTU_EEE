#
# WAITING FOR KCONFIG {
#
########################################
##### set only one following option to y, set others to n.
########################################
##WIFI-SDIO only
CONFIG_BL_SDIO ?= n
##WIFI-SDIO-BLE-SDIO combo
CONFIG_BL_BTSDU ?= n
##WIFI-SDIO-BLE-UART combo
CONFIG_BL_BTUART ?= n
##WIFI-USB only
CONFIG_BL_USB ?= n
##WIFI-USB-BLE-USB combo
CONFIG_BL_BTUSB ?= n

##cross compile options
#ARCH ?= arm
#CROSS_COMPILE ?= arm-gcc6.3-linux-uclibceabi-
KERNELDIR ?= /lib/modules/`uname -r`/build

########################################
###### Change above compile options accordingly
###### Usually no need to change configurations below
########################################
ifeq ($(CONFIG_BL_BTUART), y)
CONFIG_BL_SDIO := y
CONFIG_BL_USB := n
CONFIG_FW_COMBO ?= y
else ifeq ($(CONFIG_BL_BTSDU), y)
CONFIG_BL_SDIO := y
CONFIG_BL_USB := n
CONFIG_FW_COMBO ?= y
else ifeq ($(CONFIG_BL_BTUSB), y)
CONFIG_BL_SDIO := n
CONFIG_BL_USB := y
CONFIG_FW_COMBO ?= y
else ifeq ($(CONFIG_BL_USB), y)
CONFIG_BL_USB := y
CONFIG_BL_SDIO := n
CONFIG_FW_COMBO ?= n
else ifeq ($(CONFIG_BL_SDIO), y)
CONFIG_BL_SDIO := y
CONFIG_BL_USB := n
CONFIG_FW_COMBO ?= n
else
$(info "NO OPTION SET. At least one option should be set. Use WIFI USB by default")
CONFIG_BL_USB := y
CONFIG_FW_COMBO ?= n
endif

$(info "CONFIG_BL_SDIO is $(CONFIG_BL_SDIO)")
$(info "CONFIG_BL_BTSDU is $(CONFIG_BL_BTSDU)")
$(info "CONFIG_BL_BTUART is $(CONFIG_BL_BTUART)")
$(info "CONFIG_BL_USB is $(CONFIG_BL_USB)")
$(info "CONFIG_BL_BTUSB is $(CONFIG_BL_BTUSB)")
$(info "CONFIG_FW_COMBO is $(CONFIG_FW_COMBO)")

export CONFIG_FW_COMBO

## FW bin mode or convert to array mode
CONFIG_BL_DNLD_FWBIN ?= y

CONFIG_BL_DBG ?= n
CONFIG_BL_DYN_DBG ?= y

# user speicify the configure file location (tx_pwr.conf rf_para.conf)
CONFIG_CFG_FILE_LOCATION_USER_DEFINE ?= y

# configure usb use 2k rx buffer in driver
CONFIG_BL_USB_RX_BUF_2K ?=n

## use cfg80211 wext standard ioctl to support wext mode
CONFIG_SUPPORT_WEXT_MODE ?= n

## support wpa3 compat if apply patch to kernel (<4.17) to support wpa3
CONFIG_WPA3_SUPPORT ?=n

## mp cmds
CONFIG_BL_MP ?= y

export CONFIG_BL_DBG
export CONFIG_BL_DYN_DBG
export CONFIG_BL_MP
export CONFIG_FW_COMBO
export CONFIG_BL_DNLD_FWBIN

## force to no debug fs, avoid to inherent kernel configuration
CONFIG_DEBUG_FS := n

#
# mesh feature support
CONFIG_MESH ?=n

#
# FW ARCH:
CONFIG_BL_SDM ?= n
CONFIG_BL_TL4 ?= n
CONFIG_BL_FPGA ?= n
CONFIG_BL_RX_REORDER ?= y
CONFIG_BL_RX_DEFRAG ?= y
CONFIG_BL_BAND_5G ?= n

# IPC version
CONFIG_BL_OLD_IPC ?= n

# TX/RX profiling
CONFIG_BL_TRX_PROFILE ?= n

# Polling interrupt
CONFIG_BL_INT_POLLING ?= n

ifeq ($(CONFIG_DEBUG_FS),y)
CONFIG_BL_DEBUGFS := n
export CONFIG_BL_DEBUGFS
# Support of P2P DebugFS for enabling/disabling NoA and OppPS
CONFIG_BL_P2P_DEBUGFS ?= n
# DEBUG OPTIONS
CONFIG_BL_UM_HELPER_DFLT ?= "/dini/dini_bin/bl_umh.sh"
else
CONFIG_BL_DEBUGFS := n
export CONFIG_BL_DEBUGFS
# Support of P2P DebugFS for enabling/disabling NoA and OppPS
CONFIG_BL_P2P_DEBUGFS ?= n
# DEBUG OPTIONS
CONFIG_BL_UM_HELPER_DFLT ?= "/dini/dini_bin/bl_umh.sh"
endif

#
# } // WAITING FOR KCONFIG
#
subdir-ccflags-$(CONFIG_MESH) += -DCONFIG_MESH

subdir-ccflags-$(CONFIG_BL_DEBUGFS) += -DCONFIG_BL_DEBUGFS
subdir-ccflags-$(CONFIG_BL_DEBUGFS) += -DCONFIG_BL_UM_HELPER_DFLT=\"$(CONFIG_BL_UM_HELPER_DFLT)\"
subdir-ccflags-$(CONFIG_BL_P2P_DEBUGFS) += -DCONFIG_BL_P2P_DEBUGFS

# FW VARS
subdir-ccflags-y += -DNX_VIRT_DEV_MAX=2
subdir-ccflags-y += -DNX_REMOTE_STA_MAX=4
subdir-ccflags-y += -DNX_MU_GROUP_MAX=62
subdir-ccflags-y += -DNX_TXDESC_CNT=64
subdir-ccflags-y += -DNX_TX_MAX_RATES=4
subdir-ccflags-y += -DNX_CHAN_CTXT_CNT=3
subdir-ccflags-y += -DCONFIG_BL_FULLMAC
subdir-ccflags-y += -DDEBUG

# FW ARCH:
subdir-ccflags-$(CONFIG_BL_SDM) += -DCONFIG_BL_SDM
subdir-ccflags-$(CONFIG_BL_TL4) += -DCONFIG_BL_TL4
subdir-ccflags-$(CONFIG_BL_OLD_IPC) += -DCONFIG_BL_OLD_IPC
subdir-ccflags-$(CONFIG_BL_RX_REORDER) += -DBL_RX_REORDER
subdir-ccflags-$(CONFIG_BL_RX_DEFRAG)  += -DBL_RX_DEFRAG
subdir-ccflags-$(CONFIG_BL_FPGA) += -DBL_FPGA
subdir-ccflags-$(CONFIG_BL_BAND_5G) += -DBL_BAND_5G
subdir-ccflags-$(CONFIG_BL_TRX_PROFILE) += -DBL_TRX_PROFILE

# driver configure
subdir-ccflags-$(CONFIG_BL_DNLD_FWBIN) += -DCONFIG_BL_DNLD_FWBIN
subdir-ccflags-$(CONFIG_BL_MP) += -DCONFIG_BL_MP
subdir-ccflags-$(CONFIG_FW_COMBO) += -DCONFIG_FW_COMBO
subdir-ccflags-$(CONFIG_SUPPORT_WEXT_MODE) += -DCONFIG_SUPPORT_WEXT_MODE
subdir-ccflags-$(CONFIG_BL_USB_RX_BUF_2K) += -DBL_USB_RX_BUF_2K

subdir-ccflags-$(CONFIG_CFG_FILE_LOCATION_USER_DEFINE) += -DCFG_FILE_LOCATION_USER_DEFINE
subdir-ccflags-$(CONFIG_BL_INT_POLLING) += -DBL_INT_POLLING

subdir-ccflags-$(CONFIG_WPA3_SUPPORT) += -DBL_WPA3_COMPAT

subdir-ccflags-y += -DCONFIG_MERGE_TRAFFIC_IND_REQ
subdir-ccflags-y += -DCONFIG_PRIV_CH_SWITCH

subdir-ccflags-y += -DCONFIG_KE_TASKLET

ifeq ($(CONFIG_BL_SDIO),y)
obj-m  += sdio/
endif
ifeq ($(CONFIG_BL_USB),y)
obj-m   += usb/
endif

ifeq ($(CONFIG_BL_BTUART),y)
export  CONFIG_BL_BTUART
endif

ifeq ($(CONFIG_BL_BTSDU),y)
export CONFIG_BL_BTSDU
endif

ifeq ($(CONFIG_BL_BTUSB),y)
export CONFIG_BL_BTUSB
endif

all: modules

modules clean:
	$(MAKE) -C $(KERNELDIR) O=$(KBUILDDIR) M=$(PWD) $@
	#$(MAKE) -C $(KERNELDIR) O=$(KBUILDDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) $@

