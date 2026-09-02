# Snapshot / hardware-diff workflow

MM PS5 API Mapper can compare resolved captures to highlight stable and changing parts of the observable runtime graph.

## Workflow

1. Produce two independent read-only mapper captures.
2. Resolve each JSONL capture with `tools/resolve_map.py`.
3. Keep each resolved result in a separate directory.
4. Run `tools/compare_runtime_graph.py` against the two resolved directories.
5. Review process, thread, VM, module, API provider/consumer, `/dev`, FD, bus-device, driver, method, and resource-graph changes.

Comparison output is evidence only. Address or byte stability across captures does not assign undocumented semantics to an unknown field.

Firmware-specific captures and diff results should be stored outside the source repository.
