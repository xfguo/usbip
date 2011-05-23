bin_PROGRAMS = usbip usbipd bind_driver
usbip_SOURCES = vhci_attach.c usbip_network.c usbip_network.h
usbipd_SOURCES = stub_server.c usbip_network.c usbip_network.h
bind_driver_SOURCES = bind-driver.c utils.c utils.h
bind_driver_CFLAGS = -Wall -g @PACKAGE_CFLAGS@
bind_driver_LDADD = @PACKAGE_LIBS@
AM_CPPFLAGS = -I../include -Wall -std=gnu99
#INCLUDES = -I../include
LDADD = ../lib/libusbip.la
EXTRA_CFLAGS = @EXTRA_CFLAGS@
AM_CFLAGS = -Wall -W -Wstrict-prototypes $(EXTRA_CFLAGS)

