CFLAGS := -Wall -O2 -std=c++11 -Iinclude -pthread $(shell pkg-config --cflags libpulse ncursesw)
LIBS := $(shell pkg-config --libs libpulse ncursesw)

SRCFOLDER=src
INCFOLDER=include
OBJFOLDER=build
BINFOLDER=bin

SOURCES := $(shell find $(SRCFOLDER) -name '*.cpp')
OBJECTS := $(patsubst $(SRCFOLDER)/%.cpp,$(OBJFOLDER)/%.o,$(SOURCES))

NAME=pamix
BINARY=$(BINFOLDER)/$(NAME)

.PHONY: all clean install
all: $(BINARY)

$(BINARY): $(OBJECTS)
	@if [ ! -d $(dir $@) ]; then mkdir -p $(dir $@) ; fi
	$(CXX) $(CFLAGS) $(LIBS) $(OBJECTS) -o $(BINARY)
	
$(OBJFOLDER)/%.o: $(SRCFOLDER)/%.cpp $(wildcard $(INCFOLDER)/%.h $(INCFOLDER)/%.hpp)
	@if [ ! -d $(dir $@) ]; then mkdir -p $(dir $@) ; fi
	$(CXX) $(CFLAGS) -c $< -o $@

install: $(BINARY)
	mkdir -p "$(DESTDIR)/usr/bin"
	cp $(BINARY) "$(DESTDIR)/usr/bin"

clean:
	@-rm -rf $(BINARY) $(OBJECTS)
