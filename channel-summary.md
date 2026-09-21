## Channel RTT / delivery

RTT is measured per request ID, including send queue delay. Missing responses are excluded from RTT and included in delivery loss.
Values are medians across runs. Throughput includes the fixed drain interval. Latency changes are informational.

| Scenario | Channel | Delivery % | Responses/s | p50 ms | p95 ms | p99 ms | Previous p95 delta | Delivery delta (pp) |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| unreliable-only | unreliable | 99.600 | 385.518 | 0.154 | 0.252 | 0.293 | +1.24% | -0.200 |
| reliable-baseline | reliable | 100.000 | 39.275 | 0.140 | 0.179 | 0.233 | -8.11% | 0.000 |
| mixed | reliable | 100.000 | 40.750 | 0.133 | 0.219 | 0.301 | +19.84% | 0.000 |
| mixed | unreliable | 99.900 | 407.089 | 0.172 | 0.348 | 0.557 | +32.11% | 0.000 |

Reliable p95 under mixed load versus reliable-only: +22.11%.
