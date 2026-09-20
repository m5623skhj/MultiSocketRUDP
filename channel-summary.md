## Channel RTT / delivery

RTT is measured per request ID, including send queue delay. Missing responses are excluded from RTT and included in delivery loss.
Values are medians across runs. Throughput includes the fixed drain interval. Latency changes are informational.

| Scenario | Channel | Delivery % | Responses/s | p50 ms | p95 ms | p99 ms | Previous p95 delta | Delivery delta (pp) |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| unreliable-only | unreliable | 99.800 | 391.934 | 0.159 | 0.249 | 0.337 | -23.25% | 0.300 |
| reliable-baseline | reliable | 100.000 | 40.680 | 0.152 | 0.195 | 0.321 | -21.47% | 0.000 |
| mixed | reliable | 100.000 | 40.425 | 0.128 | 0.182 | 0.260 | -17.12% | 0.000 |
| mixed | unreliable | 99.900 | 401.018 | 0.173 | 0.264 | 0.352 | -26.46% | 0.400 |

Reliable p95 under mixed load versus reliable-only: -6.36%.
