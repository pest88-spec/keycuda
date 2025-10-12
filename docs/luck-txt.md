# luck.txt Handling Guide

- Location: repository root (`luck.txt`, newline-delimited entries).
- Contents: `<hex_scalar> <Base58_address>` for every validated match.
- Source control: excluded via `.gitignore`; archive output securely after each successful run.
- Automation: solver appends atomically; post-run scripts copy the file into `reports/` archives when evidence is generated.
