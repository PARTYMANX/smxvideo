# smxvideo

smxvideo is an ffmpeg-based video player meant to output to StepManiaX stages.  It's extremely hacked together and some stuff is almost definitely not working right.

Demo:
https://www.youtube.com/watch?v=VmrbQQdiSBo

## Building

The program is intended to be built using CMake and Visual Studio on Windows.

first, install ffmpeg:x86-windows and sdl2:x86-windows using vcpkg.  Then create a build subdirectory and run cmake to generate the project.

for example:
mkdir build
cd build
cmake -A Win32 .. -DCMAKE_TOOLCHAIN_FILE=C:/util/vcpkg/scripts/buildsystems/vcpkg.cmake

I was too lazy to set the SMX SDK up as a proper library, so try running the build once.  It will fail due to SMX.dll and SMX.lib being missing in the output directory.  copy those files from thirdparty/stepmaniax-sdk/ to build/[profile] and run the build again.  It should succeed now.

## Running

just drag a video file into the executable or call it from the command line with the video as an argument.  Currently, it will queue every frame of the video then play it back after the fact, so make sure you have the memory available to hold the whole video you're playing.  Also the program doesn't really terminate correctly, so that could be improved...