# PAMix - the pulseaudio terminal mixer

![alt tag](http://i.imgur.com/NuzrAXZ.gif)

# Keybindings #

| Effect                     | Key |
|----------------------------|-----|
| Playback tab               | F1  |
| Recording Tab              | F2  |
| Output Devices             | F3  |
| Input Devices              | F4  |
| Decrease Volume            | h   |
| Increase Volume            | l   |
| Select Next                | j   |
| Jump to next Entry         | J   |
| Select Previous            | k   |
| Jump to previous Entry     | K   |
| (Un)Mute                   | m   |
| Next/Previous device/port  | s/S |
| (Un)Lock Channels together | c   |
| Quit                       | q   |


# Dependencies: #
	- PulseAudio
	- Ncurses

# Installation: #
## Gentoo ##
`emerge media-sound/pamix`

## Arch ##
`yaourt -S pamix-git`

# Building Manually: #

## Configuration ##
Generate configure script by running `autoreconf -i` and then run `./configure` with your preferred options

### Options ###
`--disable-unicode` depends on ncurses instead of ncursesw and replaces unicode symbols with ascii

## Building ##
Run `make`

## Installing ##
Run `make install`

