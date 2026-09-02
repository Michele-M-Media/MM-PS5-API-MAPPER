# MM PS5 API Mapper v0.8

**MM PS5 API Mapper** is a read-only PS5 research and system-mapping framework designed to build a structured picture of the console surface visible to a payload at runtime.

Instead of researching one API, process, module, device, descriptor, or subsystem at a time, the mapper collects the observable information in one pipeline, normalizes it, preserves provenance, and exports it as machine-readable evidence for offline analysis and correlation.

> **Observed data is evidence, not an assumption.** Unknown fields remain raw, candidate relationships remain candidates, and exact relationships are promoted only when the captured data supports them.

## What the mapper collects from the PS5

### Filesystem and executable surface

The mapper inventories observable filesystem paths and executable/system objects relevant to mapping. For ELF/SELF material it can record:

- filesystem inventory records;
- discovered ELF/SELF objects;
- executable and module identity information;
- program and dynamic-table metadata;
- strings and symbol-table evidence where observable;
- imported and exported symbols;
- relocations;
- library/provider relationships;
- NID-bearing API records for offline enrichment.

### Process and runtime topology

The live mapper can record:

- processes and PIDs;
- loaded runtime modules;
- runtime dynamic-library structures;
- imports and exports visible through mapped runtime structures;
- process-to-module relationships;
- threads associated with mapped processes;
- process resource anchors;
- explicit coverage gaps when a branch cannot be completed safely.

### Virtual memory

The mapper records observable VM topology, including:

- process VM-space anchors;
- VM regions;
- module-to-VM relationships;
- clean VM-list terminators;
- runtime limits and observable gaps.

This allows offline reconstruction of relationships such as:

```text
process -> VM region -> module -> library -> API
```

without hard-coding a firmware map into the source tree.

### File descriptors and resource objects

The mapper can enumerate observable file-descriptor tables and preserve relationships between processes, descriptors, and associated resources.

It can record:

- FD tables per process;
- occupied file descriptors;
- descriptor-to-resource anchors;
- bounded raw resource-object prefixes;
- aligned pointer-like qwords inside bounded observations;
- repeated/shared resource evidence for offline clustering.

Raw resource observations deliberately remain raw when their undocumented semantics are not independently established.

### `/dev`, bus, devices, and drivers

The mapper can build an observable hardware-resource graph from the surface available to the payload.

It can record:

- `/dev` inventory entries;
- bus-list anchors and traversal state;
- observable bus devices;
- bounded raw candidate values associated with bus nodes;
- printable candidate strings from bounded observations;
- driver objects;
- driver method-table observations;
- softc/resource anchors where exposed;
- traversal terminators and consistency information;
- snapshot-stability evidence.

### APIs, symbols, NIDs, and metadata enrichment

The raw map can be enriched offline with:

- PS5 Payload SDK metadata;
- installed SDK libraries where available;
- source stub information;
- header-derived symbol information;
- aerolib/NID metadata;
- static and runtime symbol records captured by the mapper.

This lets unresolved NIDs and symbol relationships become a more useful developer-facing API dataset without baking those databases into the payload.

## What the mapper correlates

MM PS5 API Mapper is designed to build an **evidence graph**, not only isolated dumps.

Depending on the captured surface, the offline pipeline can correlate:

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

An **exact pointer match** means numeric equality between independently captured addresses. It does not automatically assign an undocumented private field name. Lexical and structural similarities remain explicitly marked as candidates.

## Evidence policy

The mapper follows a conservative evidence model:

- read-only observation is preferred over active probing;
- unknown fields stay unknown instead of receiving invented names;
- candidate pointers are not automatically dereferenced;
- discovered APIs are not automatically executed;
- API presence does not prove permission to invoke it;
- repeated bytes may form fingerprint families without claiming undocumented semantics;
- exact correlations require independently captured evidence;
- coverage gaps and observable limits are emitted explicitly;
- console captures and firmware-specific addresses are kept separate from the public source code.

Credential, authentication, authid, and capability structures are outside the mapper's public output policy.

## Output produced by a run

A hardware run writes its capture under:

```text
/data/MM_PS5_API_MAP/
```

The primary capture format is JSONL so each record remains independently parseable.

The included PC-side tools can derive CSV/JSON datasets for:

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
- resource clusters and fingerprints;
- graph edges;
- coverage and observable-limit reports.

See [`docs/OUTPUT_FORMAT.md`](docs/OUTPUT_FORMAT.md) and [`docs/SNAPSHOT_WORKFLOW.md`](docs/SNAPSHOT_WORKFLOW.md).

## Why this exists for developers

The project is intended as a reusable research layer for PS5 development rather than a one-off dump payload.

Mapper output can help developers:

- discover visible libraries, modules, and APIs;
- resolve NIDs against available SDK/header metadata;
- understand process, thread, and VM topology;
- correlate runtime modules with VM regions;
- inspect descriptor/resource relationships without guessing undocumented layouts;
- compare device and driver surfaces;
- build external firmware-by-firmware datasets;
- compare captures and identify stable versus changing relationships;
- build diagnostics, telemetry, visualization, compatibility, or research tools;
- reuse one structured evidence database instead of repeating low-level discovery for every project.

## Evidence states

The mapper intentionally separates:

- **observed** — directly captured evidence;
- **exactly correlated** — independently captured values that match exactly;
- **candidate** — useful relationships that still require confirmation;
- **unknown/raw** — preserved data with no invented semantic label;
- **coverage gap / observable limit** — a branch intentionally left unresolved.

## Repository layout

```text
src/        PS5 mapper implementation
include/    mapper and parser headers
tools/      offline resolver, comparison, SDK/header/NID enrichment tools
tests/      synthetic host-side regression tests
docs/       output format, workflow, test plan, and technical references
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

## Official public prebuilt ELF

The official **MM PS5 API Mapper v0.8** prebuilt ELF is publicly distributed through the GitHub **Releases** page. It is not stored inside the source tree.

**Public Release:** [v0.8 — Resource Chain Correlation Graph](https://github.com/Michele-M-Media/MM-PS5-API-MAPPER/releases/tag/v0.8)

Official asset:

```text
mm_ps5_api_mapper_v0.8_resource_chain_correlation_graph.elf
```

SHA-256:

```text
2adb6cd3967ec25b5bb93021506cd0360ae254b709d43e66a6b496315fd15a18
```

A matching `.sha256` file is attached to the same public GitHub Release. The ELF in Releases is the official downloadable v0.8 prebuilt and is publicly available.

## Validation policy

Host-side tests validate what can be tested without a console, including parser behavior, runtime-layout code paths, resolver behavior, SDK-layout fallbacks, and structural assertions.

Hardware and firmware validation results are kept separate from the clean source repository. The repository itself intentionally contains no console captures, local IPs, firmware-specific address dumps, or personal hardware baselines. This separation does **not** apply to the official ELF release, which is public and downloadable from GitHub Releases.

## License

The project is licensed under **GPL-3.0-or-later**. See [`LICENSE`](LICENSE).
