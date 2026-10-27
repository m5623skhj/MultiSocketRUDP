## Channel RTT / delivery

RTT is measured per request ID, including send queue delay. Missing responses are excluded from RTT and included in delivery loss.
Values are medians across runs. Throughput includes the fixed drain interval. Latency changes are informational.

| Scenario | Channel | Delivery % | Responses/s | p50 ms | p95 ms | p99 ms | Previous p95 delta | Delivery delta (pp) |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| unreliable-only | unreliable | 99.400 | 387.257 | 0.200 | 0.301 | 0.368 | -7.81% | 0.500 |
| reliable-baseline | reliable | 100.000 | 47.405 | 0.179 | 0.273 | 0.301 | +40.68% | 0.000 |
| mixed | reliable | 100.000 | 40.126 | 0.182 | 0.283 | 0.370 | +9.19% | 0.000 |
| mixed | unreliable | 98.600 | 395.639 | 0.232 | 0.396 | 0.577 | +14.60% | -1.000 |

Reliable p95 under mixed load versus reliable-only: +3.82%.
