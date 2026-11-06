## Channel RTT / delivery

RTT is measured per request ID, including send queue delay. Missing responses are excluded from RTT and included in delivery loss.
Values are medians across runs. Throughput includes the fixed drain interval. Latency changes are informational.

| Scenario | Channel | Delivery % | Responses/s | p50 ms | p95 ms | p99 ms | Previous p95 delta | Delivery delta (pp) |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| unreliable-only | unreliable | 99.400 | 386.309 | 0.225 | 0.331 | 0.390 | -5.30% | 0.000 |
| reliable-baseline | reliable | 100.000 | 39.340 | 0.207 | 0.283 | 0.298 | -6.18% | 0.000 |
| mixed | reliable | 100.000 | 40.100 | 0.172 | 0.274 | 0.320 | -4.30% | 0.000 |
| mixed | unreliable | 99.800 | 399.092 | 0.228 | 0.393 | 0.466 | +2.45% | 0.100 |

Reliable p95 under mixed load versus reliable-only: -3.15%.
