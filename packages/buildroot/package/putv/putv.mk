################################################################################
#
# putv
#
################################################################################

#PUTV_VERSION = 2.0
#PUTV_SOURCE = v$(OUISCLOUD_VERSION).tar.gz
#PUTV_SITE = https://github.com/ouistiti-project/putv/archive
PUTV_VERSION = HEAD
PUTV_SITE = https://github.com/ouistiti-project/putv.git
PUTV_SITE_METHOD = git
PUTV_LICENSE = MIT
PUTV_LICENSE_FILES = LICENSE
PUTV_DEPENDENCIES += libmad flac lame jansson sqlite libid3tag
PUTV_MAKE=$(MAKE1)

ifeq ($(BR2_PACKAGE_TINYSVCMDNS),y)
PUTV_DEPENDENCIES += tinysvcmdns
endif

#PUTV_MAKE_OPTS+=V=1
#PUTV_MAKE_OPTS+=DEBUG=y
PUTV_MAKE_OPTS+=SYSROOT=$(STAGING_DIR)

ifeq ($(BR2_PACKAGE_PUTV_WEBIF_HTTPS),y)
PUTV_DATADIR=/srv/wwwS
else
PUTV_DATADIR=/srv/www
endif
PUTV_MAKE_OPTS+=prefix=/usr
PUTV_MAKE_OPTS+=datadir=$(PUTV_DATADIR)

PUTV?=ouiradio
BR2_PACKAGE_PUTV_DEFCONFIG?=$(PUTV:%=%_)defconfig
PUTV_KCONFIG_DEFCONFIG=$(call qstrip,$(BR2_PACKAGE_PUTV_DEFCONFIG))
PUTV:=$(PUTV_KCONFIG_DEFCONFIG:%_defconfig=%)
PUTV_KCONFIG_OPTS = $(PUTV_MAKE_OPTS)

ifeq ($(BR2_PACKAGE_PUTV_UPNPRENDERER),y)
PUTV_DEPENDENCIES += gmrender-resurrect2
PUTV_GMRENDER_OPTS=$(call KCONFIG_ENABLE_OPT,UPNPRENDERER,$(@D)/.config)
endif

ifeq ($(BR2_PACKAGE_DIRECTFB),yy)
PUTV_DEPENDENCIES += directfb
PUTV_DIRECTFB_OPTS=$(call KCONFIG_ENABLE_OPT,DISPLAY_DIRECTFB,$(@D)/.config)
endif

BR2_PACKAGE_WAVESHARE_EPAPER=n
ifeq ($(BR2_PACKAGE_WAVESHARE_EPAPER),y)
PUTV_DEPENDENCIES += waveshare-epaper
PUTV_EPAPER_OPTS=$(call KCONFIG_ENABLE_OPT,DISPLAY_EPAPER,$(@D)/.config)
endif

PUTV_JSONRPC_OPTS=$(call KCONFIG_ENABLE_OPT,JSONRPC,$(@D)/.config)

define PUTV_KCONFIG_FIXUP_CMDS
	$(PUTV_JSONRPC_OPTS)
	$(PUTV_DIRECTFB_OPTS)
	$(PUTV_EPAPER_OPTS)
	$(PUTV_GMRENDER_OPTS)
endef

define PUTV_CONFIGURE_CMDS
	cat $(@D)/.config
endef

define PUTV_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE1) $(TARGET_CONFIGURE_OPTS) -C $(@D) $(PUTV_MAKE_OPTS)
endef

define PUTV_INSTALL_TARGET_CMDS
	$(TARGET_MAKE_ENV) $(MAKE1) $(TARGET_CONFIGURE_OPTS) -C $(@D) $(PUTV_MAKE_OPTS) DESTDIR=$(TARGET_DIR) install
	$(INSTALL) -D -m 644 $(PUTV_PKGDIR)/ouiradio.json \
		$(TARGET_DIR)/home/ouiradio.json
	rm $(TARGET_DIR)$(PUTV_DATADIR)/htdocs/apps/ouiradio.json
	ln -s /home/ouiradio.json $(TARGET_DIR)$(PUTV_DATADIR)/htdocs/apps/ouiradio.json
	$(INSTALL) -D -m 644 $(PUTV_PKGDIR)/radio.db \
		$(TARGET_DIR)/home/radio.db
	sed "s/\%PUTV\%/$(PUTV)/g" $(PUTV_PKGDIR)/putv.in > $(TARGET_DIR)/etc/setting.d/putv.in
	$(INSTALL) -D -m 755 $(PUTV_PKGDIR)/putv.sh \
		$(TARGET_DIR)/etc/setting.d/putv.sh
endef

define PUTV_INSTALL_INIT_SYSV
	$(INSTALL) -D -m 755 $(PUTV_PKGDIR)/S50putv \
		$(TARGET_DIR)/etc/init.d/S50putv
	$(INSTALL) -D -m 755 $(PUTV_PKGDIR)/S51gmrender \
		$(TARGET_DIR)/etc/init.d/S51gmrender
endef

$(eval $(kconfig-package))
