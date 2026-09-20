## Channel RTT / delivery

RTT is measured per request ID, including send queue delay. Missing responses are excluded from RTT and included in delivery loss.
Values are medians across runs. Throughput includes the fixed drain interval. Latency changes are informational.

| Scenario | Channel | Delivery % | Responses/s | p50 ms | p95 ms | p99 ms | Previous p95 delta | Delivery delta (pp) |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| unreliable-only | unreliable | 99.500 | 386.564 | 0.194 | 0.325 | 0.429 | +74.66% | -0.200 |
| reliable-baseline | reliable | 100.000 | 40.051 | 0.170 | 0.248 | 0.286 | +48.98% | 0.000 |
| mixed | reliable | 100.000 | 41.002 | 0.151 | 0.220 | 0.296 | +43.45% | 0.000 |
| mixed | unreliable | 99.500 | 407.149 | 0.214 | 0.358 | 0.434 | +90.08% | -0.400 |

Reliable p95 under mixed load versus reliable-only: -11.28%.
