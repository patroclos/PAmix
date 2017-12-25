# PAMix - the pulseaudio terminal mixer

# Table of Contents #
1. [**Installation**](#installation)
	1. Gentoo
	2. Arch
2. [**Building Manually**](#building-manually)
	1. Dependencies
	2. Configuration
	3. Building
	4. Installing
3. [**Configuration**](#configuration)
4. [**Default Keybindings**](#default-keybindings)

![alt tag](http://i.imgur.com/NuzrAXZ.gif)

# Installation: #
### Gentoo ###
`emerge media-sound/pamix`

### Arch ###
`yaourt -S pamix-git`

# Building Manually: #
## Dependencies: #
### Build ##
* cmake

### Runtime ##
* PulseAudio
* Ncurses


```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=RELEASE -DWITH_UNICODE=1
make
sudo make install
```

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

