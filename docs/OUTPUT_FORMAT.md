# MM PS5 API Mapper v0.8 JSONL records

Each line is an independent JSON object. v0.8 retains every earlier static/runtime/API/bus record and adds resource-object capture for exact offline correlation.

## New v0.8 records

### `runtime_fd_data_raw`
Bounded raw prefix read from the resource/data object already returned by the source-grounded PS5 FD walk.

Fields: `pid`, `fd`, `file_data`, `size`, `read_rc`, `interpretation`, optional `hex`.

### `runtime_fd_data_pointer_candidate`
Aligned qword in the above raw resource prefix that looks like a kernel pointer.

Fields: `pid`, `fd`, `file_data`, `offset`, `pointer`, `evidence`.

The pointer is **not dereferenced** by this record path and the raw offset is not assigned a semantic field name.

## New resolver outputs

- `runtime_fd_data_raw.csv`
- `runtime_fd_data_pointer_candidates.csv`
- `fd_resource_pointer_matches.csv`
- `bus_raw_pointer_matches.csv`
- `fd_resource_pointer_clusters.csv`
- `hardware_resource_graph.csv`

`EXACT-POINTER-MATCH` means numeric pointer equality against an independently captured object address. It does not imply a guessed field name. `/dev` lexical joins remain `CANDIDATE`.
