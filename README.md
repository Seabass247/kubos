# KubOS Core Flight Middleware

[![Build Status](https://travis-ci.org/openkosmosorg/kubos-core.svg?branch=master)](https://travis-ci.org/openkosmosorg/kubos-core) [![Join the chat at https://gitter.im/openkosmosorg/kubos-core](https://badges.gitter.im/Join%20Chat.svg)](https://gitter.im/openkosmosorg/kubos-core?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)


## Building

1. Clone our top level [Kubos project](https://github.com/openkosmosorg/KubOS)

2. Bootstrap our projects (this will also link the local yotta modules)

        $ cd KubOS

        $ ./bootstrap.sh

3. Setup your build environment:

    1. We recommend using Docker to quickly setup your environment. Our Dockerfile and instructions can be found [here](https://github.com/openkosmosorg/KubOS-rt)

    2. Want to build locally? Be sure to install these first

        1. Install ARM's [yotta build system](http://yottadocs.mbed.com/#installing)
        2. Install CMake 3.x
        3. Install the [ARM GCC toolchain](https://github.com/RIOT-OS/RIOT/wiki/Family:-ARM)

4. Navigate to KubOS-Core

        $ cd kubos-core


5. Build!

        $ yotta build
