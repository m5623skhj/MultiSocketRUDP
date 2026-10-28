## Channel RTT / delivery

RTT is measured per request ID, including send queue delay. Missing responses are excluded from RTT and included in delivery loss.
Values are medians across runs. Throughput includes the fixed drain interval. Latency changes are informational.

| Scenario | Channel | Delivery % | Responses/s | p50 ms | p95 ms | p99 ms | Previous p95 delta | Delivery delta (pp) |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| unreliable-only | unreliable | 99.400 | 387.519 | 0.125 | 0.203 | 0.303 | -32.40% | 0.000 |
| reliable-baseline | reliable | 100.000 | 40.477 | 0.136 | 0.182 | 0.211 | -33.32% | 0.000 |
| mixed | reliable | 100.000 | 39.648 | 0.106 | 0.169 | 0.247 | -40.44% | 0.000 |
| mixed | unreliable | 98.900 | 395.691 | 0.138 | 0.224 | 0.386 | -43.47% | 0.300 |

Reliable p95 under mixed load versus reliable-only: -7.26%.
