# Lite GL

A lightweight text editor with GPU-accelerated rendering, forked from [Lite XL](https://github.com/lite-xl/lite-xl).

![screenshot-dark]

## Project Status

⚠️ **Note: This project is currently under active development and is not yet ready for general use.**

Lite GL is an experimental fork of Lite XL that aims to improve performance by leveraging graphics acceleration for the rendering pipeline. While the base editor is derived from the mature Lite XL codebase, the GL-accelerated features are still being developed.

## Overview

Lite GL maintains the core philosophy of being a lightweight, Lua-based text editor while introducing hardware acceleration to reduce CPU usage and improve rendering performance. The project aims to provide:

* GPU-accelerated composition
* Reduced CPU usage compared to software rendering
* All the existing features of Lite XL
* Partial compatibility with existing Lite XL plugins (planned)

## Current Development Focus

* Implementing the GL-based rendering pipeline
* Ensuring stable performance across different platforms
* Maintaining compatibility with the Lite XL plugin ecosystem
* Setting up project infrastructure

## Building from Source

The build process is currently similar to Lite XL, but may require additional
dependencies for GL support. Detailed build instructions will be provided once
the project reaches a more stable state.

Basic build steps (subject to change):

```sh
meson setup --buildtype=release build
meson compile -C build
```

## Contributing

As this project is in its early stages, we welcome contributions in the following areas:

* GPU-accelerated rendering implementation
* Performance optimizations
* Build system improvements
* Documentation
* Testing across different platforms

## License

This project is free software; you can redistribute it and/or modify it under
the terms of the MIT license. See [LICENSE] for details.

## Acknowledgments

* Based on [Lite XL](https://github.com/lite-xl/lite-xl)
* Originally derived from [lite](https://github.com/rxi/lite) by rxi

[screenshot-dark]: https://user-images.githubusercontent.com/433545/111063905-66943980-84b1-11eb-9040-3876f1133b20.png
[LICENSE]: LICENSE

