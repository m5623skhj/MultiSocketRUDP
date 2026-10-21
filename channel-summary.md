## Channel RTT / delivery

RTT is measured per request ID, including send queue delay. Missing responses are excluded from RTT and included in delivery loss.
Values are medians across runs. Throughput includes the fixed drain interval. Latency changes are informational.

| Scenario | Channel | Delivery % | Responses/s | p50 ms | p95 ms | p99 ms | Previous p95 delta | Delivery delta (pp) |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| unreliable-only | unreliable | 98.800 | 385.030 | 0.188 | 0.298 | 0.406 | -6.28% | -0.900 |
| reliable-baseline | reliable | 100.000 | 39.752 | 0.202 | 0.265 | 0.433 | +11.18% | 0.000 |
| mixed | reliable | 100.000 | 39.800 | 0.174 | 0.255 | 0.300 | -1.39% | 0.000 |
| mixed | unreliable | 97.800 | 390.395 | 0.201 | 0.320 | 0.718 | -11.34% | -2.000 |

Reliable p95 under mixed load versus reliable-only: -3.59%.
