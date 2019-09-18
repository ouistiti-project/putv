include scripts.mk

package=upnprender
version=0.0.8

bin-y+=upnprender

upnprender_SOURCES+=src/logging.c
upnprender_SOURCES+=src/main.c
upnprender_SOURCES+=src/output.c
upnprender_SOURCES+=src/output_module.c
upnprender_SOURCES+=src/output_dummy.c
upnprender_SOURCES+=src/song-meta-data.c
upnprender_SOURCES+=src/upnp.c
upnprender_SOURCES+=src/upnp_connmgr.c
upnprender_SOURCES+=src/upnp_control.c
upnprender_SOURCES+=src/upnp_device.c
upnprender_SOURCES+=src/upnp_renderer.c
upnprender_SOURCES+=src/upnp_transport.c
upnprender_SOURCES+=src/variable-container.c
upnprender_SOURCES+=src/webclient.c
upnprender_SOURCES+=src/webserver.c
upnprender_SOURCES+=src/xmldoc.c
upnprender_SOURCES+=src/xmlescape.c

upnprender_CFLAGS+=-DHAVE_CONFIG_H
upnprender_CFLAGS+=-I$(srcdir)
upnprender_LIBRARY+=upnp
upnprender_LIBRARY+=ixml
upnprender_LIBRARY+=m
upnprender_LIBRARY+=pthread
upnprender_LIBRARY+=dl

subdir-y+=data

modules-$(MODULE_GST)+=output_gstreamer
output_gstreamer_SOURCES+=src/output_gstreamer.c
output_gstreamer_CFLAGS+=-I/usr/include/glib-2.0
output_gstreamer_LIBRARY+=glib-2.0
output_gstreamer_LIBRARY+=gstreamer

modules-$(MODULE_MPG123)+=output_mpg123
output_mpg123_SOURCES+=src/output_mpg123.c
output_mpg123_LIBRARY+=mpg123

