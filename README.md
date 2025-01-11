## Overview

**ArcVP** is a simple, cross-platform video player built using FFmpeg, SDL2, and Dear ImGui.
It was developed as a learning project to explore the basics of media processing.

**Note**: This application is not intended for production use.

ArcVP supports various video formats and offers basic playback features such as speed up/down, pausing, seeking, and more. Please be aware that the program may still contain bugs and has some known memory leak issues.

## Installation

### Get Source Code
To get started, first clone this repository to your local machine:

```bash
git clone git@github.com:deltamaya/ArcVP.git
```

### Installing Dependencies

You’ll need to install the required dependencies.
For example, on macOS, you can use Homebrew:
```bash
brew install sdl2 spdlog ffmpeg
```

### Building the Project

Once the dependencies are installed,
follow these steps to build the project:

```bash
cd ./ArcVP
mkdir build && cd build
cmake ..
make
```

### Running ArcVP

After building, you can run the program using:

```bash
./ArcVP
```
