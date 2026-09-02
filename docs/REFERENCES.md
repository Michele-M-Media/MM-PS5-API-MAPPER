# Public technical references used for MM PS5 API Mapper v0.8

- `ps5-payload-dev/sdk`
  - `crt/kernel.c`
  - `crt/kernel.h`
  - `include/ps5/kernel.h`
  - source for ALLPROC, process/thread/VM helpers, FD path, dynlib layouts and firmware-aware kernel anchors.
- `cragson/a53-code-exec`
  - public PS5 bus-device observations used by the mapper bus path (`BUS_DATA_DEVICES`, global next, nameunit, softc).
- FreeBSD source lineage
  - `sys/kern/subr_bus.c` and kobj/bus structures for bounded driver/device lineage interpretation.
- Existing project NID references
  - PS5 Payload SDK stubs/installed libraries, Aerolib, Sony ELF/SELF dynamic-tag references and the established PS5 NID hash algorithm.

v0.8 adds no new write primitive and no discovered API invocation. New FD resource correlation uses only bounded raw reads at an already source-grounded resource/data pointer, followed by PC-side pointer equality joins.
