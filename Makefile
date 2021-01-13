
SONAME = 4
SOVERSION = $(SONAME).0

TARGETS = libevaftp.so libevaftp.a
OBJECTS = evaftplib.o
SOURCES = evaftplib.c

CFLAGS = -Wall $(DEBUG) -I. $(INCLUDES) $(DEFINES) -Wno-unused-variable -D_FILE_OFFSET_BITS=64 -D__unix__
LDFLAGS = -L.
DEPFLAGS =

all : $(TARGETS)

clean :
	rm -f $(OBJECTS) core *.bak
	rm -rf unshared 
	rm -f libevaftp.so.* libevaftp.so libevaftp.a


install : all
	install -m 644 libevaftp.so.$(SOVERSION) /usr/lib
	install -m 644 evaftplib.h /usr/include
	(cd /usr/lib && \
	 ln -sf libevaftp.so.$(SOVERSION) libevaftp.so.$(SONAME) && \
	 ln -sf libevaftp.so.$(SONAME) libevaftp.so)


# build without -fPIC
unshared/evaftplib.o: evaftplib.c evaftplib.h
	test -d unshared || mkdir unshared
	$(CC) -c $(CFLAGS) -D_REENTRANT $< -o $@

static : libevaftp.a

evaftplib.o: evaftplib.c evaftplib.h
	$(CC) -c $(CFLAGS) -fPIC -D_REENTRANT $< -o $@

libevaftp.a: unshared/evaftplib.o
	ar -rcs $@ $<

libevaftp.so.$(SOVERSION): evaftplib.o
	$(CC) -shared -Wl,-soname,libevaftp.so.$(SONAME) -lc -o $@ $<

libevaftp.so: libevaftp.so.$(SOVERSION)
	ln -sf $< libevaftp.so.$(SONAME)
	ln -sf $< $@

