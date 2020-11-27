# PAMix - the pulseaudio terminal mixer

# Table of Contents #
1. [**Installation**](#installation)
	1. Debian / Ubuntu (18.04+) / Raspbian
	2. Gentoo
	3. Arch
	4. Fedora
	5. openSUSE
	6. NixOS / Nix
	7. Void Linux
2. [**Building Manually**](#building-manually)
	1. Dependencies
	2. Building and Installing
	4. Build Options
3. [**Configuration**](#configuration)
4. [**Default Keybindings**](#default-keybindings)

![alt tag](http://i.imgur.com/NuzrAXZ.gif)

# Installation: #
### Debian / Ubuntu (18.04+) / Raspbian ###
`apt install pamix`

### Gentoo ###
`emerge media-sound/pamix`

### Arch ###
`yaourt -S pamix-git`

### Fedora ###

`dnf install pamix`

### openSUSE ###
`zypper in pamix`

### NixOS / Nix ###
`nix-env -i pamix`

### Void Linux ###
`xbps-install -S PAmix`

# Building Manually: #
## Dependencies: #

In order for CMake to properly inject the current version into the headers and man files it has to be inside the git repository.

### Build ##
* cmake
* pkg-config

### Runtime ##
* PulseAudio
* Ncurses

## Building and Installing
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=RELEASE -DWITH_UNICODE=1
make
sudo make install
```

## Build Options
### Unicode Support
To compile PAmix with ncurses and without unicode support pass `-DWITH_UNICODE=0` as an argument to cmake.
This will use ncurses instead of ncursesw.

### ncursesw header location
You can pass in the include location of your ncursesw header file as an argument, if you are having problems with the default setup.
So if your ncursesw header file is at /usr/include/ncw/ncurses.h you would pass `-DNCURSESW_H_INCLUDE="ncw/ncurses.h"`

# Configuration #
Configure pamix and set keybindings using pamix.conf (see [**Configuration**](https://github.com/patroclos/PAmix/wiki/Configuration) for detailed instructions)

# Default Keybindings #

(arrow keys are also supported instead of hjkl)

| Action                     | Key |
|----------------------------|-----|
| Playback tab               | F1  |
| Recording Tab              | F2  |
| Output Devices             | F3  |
| Input Devices              | F4  |
| Cards                      | F5  |
| Set volume to percentage   | 0-9 |
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

