# SPDX-License-Identifier: (GPL-2.0+ OR MIT)

$(MEDIA_MODULE_NAME)-$(CONFIG_AMLOGIC_HDMITX) += vout/hdmitx20/hdmi_tx_main.o
$(MEDIA_MODULE_NAME)-$(CONFIG_AMLOGIC_HDMITX) += vout/hdmitx20/hdmi_tx_video.o
$(MEDIA_MODULE_NAME)-$(CONFIG_AMLOGIC_HDMITX) += vout/hdmitx20/hdmi_tx_audio.o
$(MEDIA_MODULE_NAME)-$(CONFIG_AMLOGIC_HDMITX) += vout/hdmitx20/hdmi_tx_hdcp.o
$(MEDIA_MODULE_NAME)-$(CONFIG_AMLOGIC_HDMITX) += vout/hdmitx20/hdmi_tx_scdc.o
$(MEDIA_MODULE_NAME)-$(CONFIG_AMLOGIC_HDMITX) += vout/hdmitx20/meson_hdcp.o

ccflags-y += -I$(srctree)/$(COMMON_DRIVERS_DIR)/drivers/media/vout/hdmitx_common

include $(COMMON_DRIVERS_DIR)/drivers/media/vout/hdmitx20/hw/Makefile
