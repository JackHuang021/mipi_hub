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

MODULE_INFO(intree, "Y");

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

SYMBOL_CRC(sec_ts_dev, 0xfdf3d2e5, "");
SYMBOL_CRC(get_tsp_status, 0xb6302a5a, "");
SYMBOL_CRC(sec_ts_glove_mode_enables, 0xbfbc687b, "");
SYMBOL_CRC(sec_ts_hover_enables, 0xf18e30bc, "");
SYMBOL_CRC(sec_ts_firmware_update_on_hidden_menu, 0xf959367b, "");

static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0x189dbc92, "input_allocate_device" },
	{ 0x3037b86c, "filp_open" },
	{ 0xe914e41e, "strcpy" },
	{ 0xc1514a3b, "free_irq" },
	{ 0xc6d09aa9, "release_firmware" },
	{ 0xdaf9b845, "device_set_wakeup_capable" },
	{ 0x49cd25ed, "alloc_workqueue" },
	{ 0xbfc55251, "__class_create" },
	{ 0x59e418e8, "devm_kmalloc" },
	{ 0x1e39bf4, "of_property_read_variable_u32_array" },
	{ 0x7546d964, "proc_create" },
	{ 0x8ad54aec, "seq_release" },
	{ 0x656e4a6e, "snprintf" },
	{ 0xc5b6f236, "queue_work_on" },
	{ 0xb12cbacb, "fb_unregister_client" },
	{ 0x16072ada, "request_firmware" },
	{ 0x3f45b17e, "input_mt_report_slot_state" },
	{ 0x8e9b1f97, "gpiod_to_irq" },
	{ 0x81fbf1dd, "input_unregister_device" },
	{ 0x4829a47e, "memcpy" },
	{ 0x37a0cba, "kfree" },
	{ 0xfcec0987, "enable_irq" },
	{ 0x8d5e0f25, "seq_lseek" },
	{ 0xfe990052, "gpio_free" },
	{ 0xbdad8b89, "i2c_transfer_buffer_flags" },
	{ 0xc3055d20, "usleep_range_state" },
	{ 0x5b550a6, "__dynamic_dev_dbg" },
	{ 0x9513503e, "of_get_named_gpio_flags" },
	{ 0xd6c23958, "input_free_device" },
	{ 0x92997ed8, "_printk" },
	{ 0xa182fa6a, "input_register_device" },
	{ 0x8da6585d, "__stack_chk_fail" },
	{ 0xb2fcb56d, "queue_delayed_work_on" },
	{ 0xa916b694, "strnlen" },
	{ 0x481bd8ad, "_dev_info" },
	{ 0x8a2df6fb, "sysfs_create_link" },
	{ 0xdbdd5f58, "i2c_register_driver" },
	{ 0x61651be, "strcat" },
	{ 0x896fb3d1, "_dev_err" },
	{ 0x6c54c191, "device_wakeup_enable" },
	{ 0x8c8569cb, "kstrtoint" },
	{ 0x92d5838e, "request_threaded_irq" },
	{ 0xb5e27b5a, "input_mt_init_slots" },
	{ 0x36dbd15c, "device_create" },
	{ 0x8c03d20c, "destroy_workqueue" },
	{ 0x4dfa8d4b, "mutex_lock" },
	{ 0x5a921311, "strncmp" },
	{ 0x9ed12e20, "kmalloc_large" },
	{ 0xb092536b, "sysfs_create_group" },
	{ 0xcefb0c9f, "__mutex_init" },
	{ 0x2e2b40d2, "strncat" },
	{ 0xf779c8f8, "proc_mkdir" },
	{ 0xdcb764ad, "memset" },
	{ 0x4b363de8, "kernel_read" },
	{ 0xb11ca9af, "input_event" },
	{ 0x9a42b8cf, "input_set_abs_params" },
	{ 0xe2d5255a, "strcmp" },
	{ 0x49ccbb88, "pinctrl_lookup_state" },
	{ 0xfd0a7de3, "seq_read" },
	{ 0x3c3ff9fd, "sprintf" },
	{ 0x3213f038, "mutex_unlock" },
	{ 0xc6f46339, "init_timer_key" },
	{ 0xeae3dfd6, "__const_udelay" },
	{ 0x403f9529, "gpio_request_one" },
	{ 0xaa8d0771, "filp_close" },
	{ 0xd61b23f, "seq_printf" },
	{ 0xffeedf6a, "delayed_work_timer_fn" },
	{ 0x3bced318, "gpio_to_desc" },
	{ 0x3ed1b3fd, "input_mt_destroy_slots" },
	{ 0x9ca6f2b0, "devm_pinctrl_get" },
	{ 0xcba6698, "i2c_transfer" },
	{ 0x165cfce2, "pinctrl_select_state" },
	{ 0x8631c9ba, "alt_cb_patch_nops" },
	{ 0x7933e296, "seq_open" },
	{ 0xaaab637c, "kmalloc_trace" },
	{ 0x9044ceec, "i2c_del_driver" },
	{ 0x98cf60b3, "strlen" },
	{ 0x349cba85, "strchr" },
	{ 0xcc01b3d2, "of_property_read_string_helper" },
	{ 0xf9a482f9, "msleep" },
	{ 0xeb233a45, "__kmalloc" },
	{ 0x84b47761, "kmalloc_caches" },
	{ 0x2d3385d3, "system_wq" },
	{ 0x3ce4ca6f, "disable_irq" },
	{ 0xc1d284e9, "module_layout" },
};

MODULE_INFO(depends, "");

MODULE_ALIAS("i2c:sec_ts");
MODULE_ALIAS("of:N*T*Csec,sec_ts");
MODULE_ALIAS("of:N*T*Csec,sec_tsC*");

MODULE_INFO(srcversion, "07C10C1FF9C629A0DF8179A");
