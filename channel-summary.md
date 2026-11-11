## Channel RTT / delivery

RTT is measured per request ID, including send queue delay. Missing responses are excluded from RTT and included in delivery loss.
Values are medians across runs. Standalone v3 waits for each response/timeout; mixed throughput includes the drain interval. RTT includes send queue delay. Latency changes are informational.

| Scenario | Channel | Delivery % | Responses/s | p50 ms | p95 ms | p99 ms | Timeouts | Previous p95 delta | Delivery delta (pp) |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| unreliable-only | unreliable | 100.000 | 6200.351 | 0.146 | 0.174 | 0.202 | 0.000 | N/A | N/A |
| reliable-baseline | reliable | 100.000 | 4860.952 | 0.174 | 0.208 | 0.239 | 0.000 | N/A | N/A |
| mixed | reliable | 100.000 | 39.486 | 0.224 | 0.291 | 0.364 | N/A | +68.87% | 0.000 |
| mixed | unreliable | 99.600 | 393.276 | 0.266 | 0.379 | 0.486 | N/A | +69.76% | 0.000 |
