## Channel RTT / delivery

RTT is measured per request ID, including send queue delay. Missing responses are excluded from RTT and included in delivery loss.
Values are medians across runs. Throughput includes the fixed drain interval. Latency changes are informational.

| Scenario | Channel | Delivery % | Responses/s | p50 ms | p95 ms | p99 ms | Previous p95 delta | Delivery delta (pp) |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| unreliable-only | unreliable | 99.700 | 391.975 | 0.188 | 0.312 | 0.377 | -6.14% | -0.200 |
| reliable-baseline | reliable | 100.000 | 40.567 | 0.143 | 0.194 | 0.224 | -14.28% | 0.000 |
| mixed | reliable | 100.000 | 40.320 | 0.145 | 0.247 | 0.283 | -2.72% | 0.000 |
| mixed | unreliable | 99.700 | 401.994 | 0.216 | 0.376 | 0.447 | +0.08% | 0.200 |

Reliable p95 under mixed load versus reliable-only: +27.39%.
