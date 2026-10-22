## Channel RTT / delivery

RTT is measured per request ID, including send queue delay. Missing responses are excluded from RTT and included in delivery loss.
Values are medians across runs. Throughput includes the fixed drain interval. Latency changes are informational.

| Scenario | Channel | Delivery % | Responses/s | p50 ms | p95 ms | p99 ms | Previous p95 delta | Delivery delta (pp) |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| unreliable-only | unreliable | 98.900 | 389.167 | 0.164 | 0.244 | 0.302 | -18.10% | 0.100 |
| reliable-baseline | reliable | 100.000 | 39.309 | 0.156 | 0.201 | 0.291 | -24.04% | 0.000 |
| mixed | reliable | 100.000 | 39.643 | 0.124 | 0.182 | 0.260 | -28.81% | 0.000 |
| mixed | unreliable | 98.100 | 388.902 | 0.158 | 0.236 | 0.334 | -26.25% | 0.300 |

Reliable p95 under mixed load versus reliable-only: -9.65%.
