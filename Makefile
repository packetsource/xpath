# Definitions
CC = gcc
LD = gcc
CFLAGS = -g -I /usr/include/libxml2 
LDFLAGS = -g
LIB = -lxml2
OBJ = xpath.o
INCLUDE = xpath.h
EXE = xpath
PKGDEPS =
PACKAGE_NAME = xpath_1.0-1

# General compilation rule for C
%.o: %.c $(INCLUDE)
	$(CC) $(CFLAGS) -c $<

# Link
$(EXE): $(PKGDEPS) $(OBJ)
	$(LD) $(LDFLAGS) -o $@ $^ $(LIB)

# Clean object
clean:
	rm -rf $(EXE) $(OBJ) *.o
	rm -rf deb/*

# Make a debian linsux package
deb: $(EXE)
	mkdir -p deb/$(PACKAGE_NAME)
	mkdir -p deb/$(PACKAGE_NAME)/usr/local/bin
	cp $(EXE) deb/$(PACKAGE_NAME)/usr/local/bin
	-mkdir -p deb/$(PACKAGE_NAME)/etc/systemd/system
	-cp *.service deb/$(PACKAGE_NAME)/etc/systemd/system
	cp -r DEBIAN deb/$(PACKAGE_NAME)
	dpkg-deb --build deb/$(PACKAGE_NAME)
	rm -rf deb/$(PACKAGE_NAME)
