# Hardware test plan

1. Build the payload with a compatible PS5 Payload SDK.
2. Deploy it to an authorized test console using the normal payload loader path.
3. Allow the mapper to complete its read-only capture.
4. Copy the generated JSONL map and summary from `/data/MM_PS5_API_MAP/` to the analysis workstation.
5. Run the resolver and comparison tools against the returned capture.
6. Check `runtime_errors`, observable-limit records, graph consistency, and resolver output before calling a target validated.
7. Keep firmware-specific captures, addresses, logs, and result tables outside the source repository.

A host-side PASS verifies only the synthetic/local pipeline. It is not a substitute for a hardware result.
