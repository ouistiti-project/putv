################################################################################
#
# putv
#
################################################################################

#TINYSVCMDNS_VERSION = 2.0
TINYSVCMDNS_VERSION = HEAD
TINYSVCMDNS_SITE = https://bitbucket.org/geekman/tinysvcmdns
TINYSVCMDNS_SITE_METHOD = git
TINYSVCMDNS_LICENSE = BSD
TINYSVCMDNS_LICENSE_FILES = LICENSE
TINYSVCMDNS_INSTALL_STAGING=YES

TINYSVCMDNS_MAKE_OPTS= \
	prefix=/usr \
	datadir=$(TINYSVCMDNS_DATADIR)


define TINYSVCMDNS_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE1) $(TARGET_CONFIGURE_OPTS) -C $(@D) $(TINYSVCMDNS_MAKE_OPTS)
endef

TINYSVCMDNS_LIBEX=so
define TINYSVCMDNS_INSTALL_STAGING_CMDS
	$(INSTALL) -D -m 644 $(@D)/libtinysvcmdns.$(TINYSVCMDNS_LIBEX) \
		$(STAGING_DIR)/usr/lib/libtinysvcmdns.$(TINYSVCMDNS_LIBEX)
	$(INSTALL) -D -m 644 $(@D)/mdnsd.h \
		$(STAGING_DIR)/usr/include/tinysvcmdns/mdnsd.h
	$(INSTALL) -D -m 644 $(@D)/mdns.h \
		$(STAGING_DIR)/usr/include/tinysvcmdns/mdns.h
endef

define TINYSVCMDNS_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 644 $(@D)/libtinysvcmdns.$(TINYSVCMDNS_LIBEX) \
		$(STAGING_DIR)/usr/lib/libtinysvcmdns.$(TINYSVCMDNS_LIBEX)
endef

$(eval $(generic-package))
