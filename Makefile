INSTALL_PROGRAM = install
PREFIX = /usr/local

all: diskperf

.PHONY: clean install

clean:
	rm -f *.o

install: diskperf
	$(INSTALL_PROGRAM) $< $(DESTDIR)$(PREFIX)/bin/$<
