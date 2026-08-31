# ImplusOS API

Typed syscall wrappers for ImplusOS userland (`File.h`, `Graphics.h`,
`Input.h`, `Socket.h`, `Audio.h`, `WiFi.h`, `OSDebug.h`, ...) plus a
few helper libraries (`FreeType.c`, `Jpeg.c`, `XMLParser.c`,
`Zlib.h`) shared by every app and service.

This repository is a component of **[ImplusOS](https://github.com/ImplusOS)**,
a hobby operating system with a monolithic kernel, loadable driver modules,
a minimal freestanding C library, and a small graphical userland. It is not
meant to be built in isolation -- it is consumed as a checkout alongside
ImplusOS's other component repositories (see `Docs` for the full
architecture and `ImplusOS/Makefile` for how the pieces are wired together).

## Layout

```
API/
├── Source/    All source for this component, structure preserved from ImplusOS
└── README.md  This file
```

## Build

Header/source-only: no standalone Makefile. Consumed via `-I` by
[Userland-Common](https://github.com/ImplusOS/Userland-Common)'s
`AppCommon.mk` and by each app/service's own Makefile.

## License

MIT, matching the parent [ImplusOS](https://github.com/ImplusOS/ImplusOS) project.
