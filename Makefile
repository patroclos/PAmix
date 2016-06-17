CC=g++
CFLAGS=-Wall -O2 -std=c++11 -Iinclude -g
LIBS=ncursesw pulse

SRCFOLDER=src
INCFOLDER=include
OBJFOLDER=build
BINFOLDER=bin

SOURCES := $(shell find $(SRCFOLDER) -name '*.cpp')
OBJECTS := $(patsubst $(SRCFOLDER)/%.cpp,$(OBJFOLDER)/%.o,$(SOURCES))

NAME=pamix
BINARY=$(BINFOLDER)/$(NAME)

.PHONY: all clean
all: $(BINARY)

$(BINARY): $(OBJECTS)
	@if [ ! -d $(dir $@) ]; then mkdir -p $(dir $@) ; fi
	$(CC) $(CFLAGS) $(addprefix -l,$(LIBS)) $(OBJECTS) -o $(BINARY)
	
$(OBJFOLDER)/%.o: $(SRCFOLDER)/%.cpp $(wildcard $(INCFOLDER)/%.h $(INCFOLDER)/%.hpp)
	@if [ ! -d $(dir $@) ]; then mkdir -p $(dir $@) ; fi
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	@-rm -rf $(BINARY) $(OBJECTS)
