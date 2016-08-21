bin_PROGRAMS = pamix
pamix_CXXFLAGS = -I$(top_srcdir)/include -pthread $(NCURSES_CFLAGS) $(PULSEAUDIO_CFLAGS)
pamix_LDADD = $(NCURSES_LIBS) $(PULSEAUDIO_LIBS)
pamix_SOURCES = \
		src/volumeutil.cpp\
		src/sinkentry.cpp\
		src/sourceentry.cpp\
		src/pamix.cpp\
		src/sourceoutputentry.cpp\
		src/sinkinputentry.cpp\
		src/painterface.cpp

man1_MANS = man/pamix.1
