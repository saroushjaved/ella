# ELLA semantic component

This optional process provides CPU-only `intfloat/e5-small-v2` embeddings and an
HNSW passage index. It is deliberately outside the core executable and is not
built unless `ELLA_BUILD_SEMANTIC_HELPER=ON`.

Supply pinned, CPU-only ONNX Runtime and hnswlib paths:

```powershell
cmake -S . -B build-semantic -DELLA_BUILD_SEMANTIC_HELPER=ON `
  -DONNXRUNTIME_ROOT=C:\deps\onnxruntime-win-x64-1.24.1 `
  -DHNSWLIB_ROOT=C:\deps\hnswlib
cmake --build build-semantic --target ella-semantic --config Release
```

The installable ZIP uses this layout:

```text
bin/ella-semantic.exe
bin/onnxruntime.dll
model/model.onnx
model/vocab.txt
MODEL-LICENSE.txt
.ella-component.json   (written by ELLA after verification)
```

The helper resolves `model/model.onnx` and `model/vocab.txt` relative to its
executable. It may also receive `--model` and `--vocab` during development. The
core starts it with `--database` and `--index-dir`, then exchanges one JSON
object per line. Work is serialized and the client bounds its queue to four.
Supported commands are `rebuild`, `search`, and `related`.

Indexing prefixes passages with `passage: ` and queries with `query: ` as the E5
model requires. Embeddings are attention-mask mean pooled and L2-normalized.
The index metadata binds the model SHA-256, dimension, 400-token chunk contract,
and 60-token overlap. A mismatch requires a rebuild; it never causes source or
note deletion. Search groups passage neighbors by source and returns the best
evidence with stable file and passage identifiers. The core performs reciprocal
rank fusion with keyword results.

Do not publish a component catalogue entry until the model license and notices
are included, the ZIP checksum and exact sizes are pinned, malicious-archive
tests pass, and the 100-query retrieval benchmark meets the release gate.
