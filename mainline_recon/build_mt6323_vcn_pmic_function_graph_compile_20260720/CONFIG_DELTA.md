# Configuration delta

Baseline: `backup/baseline.config` (byte-for-byte copy of the MT6323 VCN
registration-audit isolated `O=` config).

Requested intentional delta before `olddefconfig`:

- `CONFIG_FUNCTION_TRACER`: `# CONFIG_FUNCTION_TRACER is not set` -> `y`
- `CONFIG_FUNCTION_GRAPH_TRACER`: absent in baseline -> `y`

No other symbol is intentionally changed. The final `olddefconfig` result and
its complete diff against the copied baseline are recorded in:

- `config_delta_requested.patch`
- `config_delta_final.patch`
- `config_delta_final.txt`
