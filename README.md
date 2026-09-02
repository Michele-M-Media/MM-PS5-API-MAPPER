# MM PS5 API Mapper v0.8

**MM PS5 API Mapper** is a read-only PS5 research and system-mapping framework created to build a structured picture of the console surface visible to a payload at runtime.

Instead of researching one API, module, process, device, descriptor, or subsystem at a time, the mapper collects the observable information in one pipeline, normalizes it, preserves its provenance, and exports it as machine-readable evidence that can be inspected and correlated offline.

The project is deliberately conservative: **observed data is evidence, not an assumption**. Unknown fields remain raw. Candidate relationships remain candidates. Exact relationships are promoted only when the captured data supports them.

## What the mapper collects from the PS5

The mapper combines static filesystem analysis with live runtime enumeration.

### Filesystem and executable surface

It inventories observable filesystem paths and searches the accessible surface for executables and system objects relevant to mapping. For ELF/SELF material it records the metadata needed to describe and cross-reference the executable surface without modifying files on the console.

This layer can provide:

- filesystem inventory records;
- discovered ELF/SELF objects;
- executable/module identity information;
- program and dynamic-table metadata;
- strings and symbol-table evidence where observable;
- imported/exported symbol information;
- relocation information;
- library/provider relationships;
- NID-bearing API records that can later be enriched offline.

### Process and runtime topology

The live mapper enumerates the observable process graph and records the runtime relationships needed to understand where code and resources exist while the system is running.

It can record:

- processes and PIDs;
- loaded runtime modules;
- dynamic-library sections and runtime tables;
- imports and exports visible through the mapped runtime structures;
- process-to-module relationships;
- threads associated with mapped processes;
- process resource anchors;
- explicit runtime coverage gaps when a branch cannot be completed safely.

### Virtual-memory surface

The mapper records the observable VM topology rather than treating a module name as sufficient proof of where it is mapped.

It can capture:

- process VM-space anchors;
- VM regions;
- module-to-VM relationships;
- clean VM-list terminators;
- runtime limits or gaps when the observable chain cannot be continued safely.

This allows offline tools to reconstruct relationships such as:

`process -> VM region -> module -> library -> API`

without embedding a hard-coded firmware map into the repository.

### File descriptors and resource objects

The mapper enumerates observable file-descriptor tables and preserves the relationships between a process, an occupied descriptor, and its associated resource/data object.

It can record:

- FD tables per process;
- occupied file descriptors;
- descriptor-to-resource anchors;
- bounded raw resource-object prefixes;
- aligned pointer-like qwords found inside those bounded observations;
- clusters of repeated or shared resource evidence for offline analysis.

Raw resource observations deliberately remain raw when their private field semantics are not independently established. A pointer-looking value is recorded as a **candidate**, not automatically assigned a Sony structure or function name.

### `/dev`, hardware bus, devices, and drivers

The mapper also builds an observable hardware-resource graph from the source-grounded bus/device surface available to the payload.

It can record:

- `/dev` inventory entries;
- bus-list anchors and traversal state;
- observable bus devices;
- bounded raw candidate values associated with bus nodes;
- printable candidate strings discovered in bounded observations;
- driver objects;
- driver method-table observations;
- softc/resource anchors where the mapped structure exposes them;
- bus-list terminators and consistency information;
- snapshot-stability evidence for the captured traversal.

The PC-side resolver can then compare independently captured addresses and build exact or candidate relationships between the FD/resource graph and the bus/device/driver graph.

### APIs, symbols, NIDs, and metadata enrichment

The raw console map can be enriched offline using the included tools and available development metadata.

The toolchain can use:

- PS5 Payload SDK metadata;
- installed SDK libraries where available;
- source stub information;
- header-derived symbol information;
- aerolib/NID metadata;
- static and runtime symbol records captured by the mapper.

This allows unresolved NIDs and symbol relationships to be turned into a more useful developer-facing API database without requiring those databases to be hard-coded into the payload.

## What the mapper correlates

Collecting isolated objects is only the first step. MM PS5 API Mapper is designed to build an **evidence graph**.

Depending on the captured surface, the offline pipeline can correlate relationships such as:

- process -> thread;
- process -> VM space -> VM region;
- process -> loaded module;
- module -> library -> import/export/API;
- process -> FD -> resource object;
- resource object -> repeated raw fingerprint family;
- bus -> device -> driver -> driver method evidence;
- raw bus pointer -> independently captured bus entity;
- resource pointer candidate -> independently captured object address;
- `/dev` name -> bus/device lexical candidate.

An **exact pointer match** means numeric equality between independently captured addresses. It does not automatically assign an undocumented private field name. Lexical matches and structural similarities remain explicitly marked as candidates.

## Data and evidence policy

MM PS5 API Mapper follows several rules intended to keep its output useful and reproducible:

- read-only observation is preferred over active probing;
- unknown fields stay unknown instead of receiving invented names;
- candidate pointers are not automatically dereferenced;
- discovered APIs are not automatically executed;
- API presence does not prove that the caller has permission to use it;
- repeated bytes may form a fingerprint family without claiming undocumented semantics;
- exact correlations require independently captured evidence;
- coverage gaps and observable limits are emitted explicitly rather than hidden;
- private console captures and firmware-specific addresses are kept outside the public source tree.

Credential, authentication, authid, and capability structures are outside the mapper's output policy.

## Output produced by a run

A hardware run writes its capture under:

```text
/data/MM_PS5_API_MAP/
```

The primary capture is JSONL so each record is independently parseable and can be processed without loading one giant proprietary database format.

The included PC-side tools can derive CSV/JSON datasets for areas including:

- filesystem/executable inventory;
- processes and runtime modules;
- threads;
- VM spaces and VM regions;
- imports, exports, symbols, NIDs, and relocations;
- FD tables and descriptors;
- bounded resource-object observations;
- resource pointer candidates;
- bus devices and drivers;
- driver methods and resource anchors;
- exact pointer joins;
- candidate lexical joins;
- resource clusters/fingerprints;
- graph edges;
- coverage and observable-limit reports.

See [`docs/OUTPUT_FORMAT.md`](docs/OUTPUT_FORMAT.md) and [`docs/SNAPSHOT_WORKFLOW.md`](docs/SNAPSHOT_WORKFLOW.md) for the processing workflow.

## Why this exists for developers

A major goal of MM PS5 API Mapper is to give developers a reusable starting point for PS5 research and homebrew development.

A developer can use mapper output to:

- discover which libraries and APIs are visible on a target console;
- resolve NIDs against available SDK/header metadata;
- understand which modules are loaded by which processes;
- correlate runtime modules with VM regions;
- inspect process/thread topology;
- study descriptor and resource relationships without guessing private layouts;
- compare device and driver surfaces;
- build firmware-by-firmware datasets externally without polluting the source tree with hard-coded captures;
- compare two captures and identify what stayed stable or changed;
- build diagnostic, telemetry, visualization, compatibility, or research tools on top of a structured map;
- use one indexed evidence database instead of repeating the same low-level discovery work for every new project.

In other words, the mapper is not intended to be only a one-off dump payload. It is a **mapping framework and reusable developer dataset generator**.

## What the project does not claim

A mapped object is not automatically a supported API contract. A discovered function is not automatically callable. A pointer-looking value is not automatically a known structure member. A device name is not automatically proof of a direct driver relationship.

The mapper separates:

- **observed** — directly captured evidence;
- **exactly correlated** — independently captured values that match exactly;
- **candidate** — useful correlation evidence that still needs confirmation;
- **unknown/raw** — preserved data with no invented semantic label;
- **coverage gap / observable limit** — a branch the mapper intentionally did not pretend to understand.

## Repository layout

```text
src/        PS5 mapper implementation
include/    mapper and parser headers
tools/      offline resolver, comparison, SDK/header/NID enrichment tools
tests/      synthetic host-side regression tests
docs/       output format, workflow, test plan, and technical references
release/    optional prebuilt release artifact and checksum
```

Important root files:

- `Makefile` — PS5 Payload SDK build entry point;
- `BUILD-WINDOWS.bat` — Windows/WSL build helper;
- `SEND-TO-PS5.bat` — deploy helper using a local `ps5_ip.txt`;
- `RESOLVE-MAP-WINDOWS.bat` — offline resolver helper;
- `COMPARE-RUNTIME-GRAPH-WINDOWS.bat` — graph comparison helper;
- `HOST-VALIDATE.sh` / `HOST-VALIDATE-WINDOWS.bat` — host-side regression validation.

## Build requirements

The payload build expects `PS5_PAYLOAD_SDK` to point to a compatible PS5 Payload SDK installation.

Host-side validation requires:

- a C compiler;
- Python 3;
- normal POSIX shell tooling, or WSL when using the supplied Windows helpers.

Local network configuration is intentionally excluded from the repository. Copy:

```text
ps5_ip.txt.example -> ps5_ip.txt
```

and set the console address locally. `ps5_ip.txt` is ignored by Git.

## Prebuilt ELF

When included in `release/`, the prebuilt ELF corresponds to the v0.8 Resource Chain Correlation Graph build. A SHA-256 file is supplied beside it so the artifact can be verified independently.

The public prebuilt ELF is stripped of compiler debug information that exposes local build paths; runtime code and the embedded mapper version remain intact.

## Validation policy

Host-side tests validate what can be tested without a console, including parser behavior, runtime-layout code paths, resolver behavior, SDK-layout fallbacks, and structural assertions.

Hardware and firmware validation belongs in external test results. The clean source repository intentionally contains **no private console capture, local IP, firmware-specific address dump, or personal hardware baseline**.

## License

The project is licensed under **GPL-3.0-or-later**. See [`LICENSE`](LICENSE).
