CC=g++
CFLAGS=-Wall -O2 -std=c++11 -Iinclude -pthread
LIBS=ncursesw pulse

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
	$(CC) $(CFLAGS) $(addprefix -l,$(LIBS)) $(OBJECTS) -o $(BINARY)
	
$(OBJFOLDER)/%.o: $(SRCFOLDER)/%.cpp $(wildcard $(INCFOLDER)/%.h $(INCFOLDER)/%.hpp)
	@if [ ! -d $(dir $@) ]; then mkdir -p $(dir $@) ; fi
	$(CC) $(CFLAGS) -c $< -o $@

install: $(BINARY)
	cp $(BINARY) /usr/bin

clean:
	@-rm -rf $(BINARY) $(OBJECTS)
