# Build display kernel bridge driver
BRIDGE_KERNEL_MODULES :=

ifneq (,$(call is-board-platform-in-list2, sun))
	BRIDGE_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/lt9611uxd.ko
endif

