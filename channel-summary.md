## Channel RTT / delivery

RTT is measured per request ID, including send queue delay. Missing responses are excluded from RTT and included in delivery loss.
Values are medians across runs. Throughput includes the fixed drain interval. Latency changes are informational.

| Scenario | Channel | Delivery % | Responses/s | p50 ms | p95 ms | p99 ms | Previous p95 delta | Delivery delta (pp) |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| unreliable-only | unreliable | 99.300 | 391.914 | 0.126 | 0.208 | 0.288 | -38.29% | -0.300 |
| reliable-baseline | reliable | 100.000 | 41.317 | 0.118 | 0.159 | 0.208 | -40.91% | 0.000 |
| mixed | reliable | 100.000 | 39.933 | 0.105 | 0.142 | 0.174 | -43.59% | 0.000 |
| mixed | unreliable | 99.500 | 397.737 | 0.134 | 0.185 | 0.226 | -52.47% | 0.100 |

Reliable p95 under mixed load versus reliable-only: -10.82%.
