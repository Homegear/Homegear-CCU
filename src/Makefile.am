aAUTOMAKE_OPTIONS = subdir-objects

AM_CPPFLAGS = -Wall -std=c++11 -DFORTIFY_SOURCE=2 -DGCRYPT_NO_DEPRECATED
AM_LDFLAGS = -Wl,-rpath=/lib/homegear -Wl,-rpath=/usr/lib/homegear -Wl,-rpath=/usr/local/lib/homegear
LIBS += -Wl,-Bdynamic

libdir = $(localstatedir)/lib/homegear/modules
lib_LTLIBRARIES = mod_ccu.la
mod_ccu_la_SOURCES = DescriptionCreator.cpp MyFamily.cpp MyPacket.cpp MyPeer.cpp Factory.cpp GD.cpp MyCentral.cpp Interfaces.cpp PhysicalInterfaces/Ccu.cpp
mod_ccu_la_LDFLAGS =-module -avoid-version -shared
install-exec-hook:
	rm -f $(DESTDIR)$(libdir)/mod_ccu.la
