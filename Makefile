INSTALL_PROGRAM = install
BINDIR = /usr/local/bin

all: diskperf

.PHONY: clean install

clean:
	rm -f *.o

install: diskperf
	$(INSTALL_PROGRAM) $< $(DESTDIR)$(BINDIR)/$<
