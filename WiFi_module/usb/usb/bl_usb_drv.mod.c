#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/export-internal.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;
BUILD_LTO_INFO;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

MODULE_INFO(depends, "");

MODULE_ALIAS("usb:v349Bp6160d*dc*dsc*dp*ic*isc*ip*in*");
MODULE_ALIAS("usb:v349Bp6180d*dc*dsc*dp*ic*isc*ip*in*");
MODULE_ALIAS("usb:v75FBp759Bd*dc*dsc*dp*ic*isc*ip*in*");
MODULE_ALIAS("usb:v349Bp6161d*dc*dsc*dp*icFFisc00ip01in*");
MODULE_ALIAS("usb:v349Bp6181d*dc*dsc*dp*icFFisc00ip01in*");

MODULE_INFO(srcversion, "AF36243146EF5D968018B65");
