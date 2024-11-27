# PAMix - the pulseaudio terminal mixer

![alt tag](http://i.imgur.com/NuzrAXZ.gif)

## Dependencies: #

### Build ##
* cmake
* pkg-config

### Runtime ##
* PulseAudio
* Ncursesw

## Building and Installing
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=RELEASE
make
sudo make install
```

# Configuration #
PAmix keybindings are configured in `$XDG_CONFIG_HOME/pamix.conf` (see [**Configuration**](https://github.com/patroclos/PAmix/wiki/Configuration) for detailed instructions)

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
| Select Next                | j   |
| Select Previous            | k   |
| Increase Volume            | l   |
| (Un)Lock Channels          | c   |
| (Un)Mute                   | m   |
| Next/Previous device/port  | s/S |
| Quit                       | q   |

