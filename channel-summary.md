## Channel RTT / delivery

RTT is measured per request ID, including send queue delay. Missing responses are excluded from RTT and included in delivery loss.
Values are medians across runs. Throughput includes the fixed drain interval. Latency changes are informational.

| Scenario | Channel | Delivery % | Responses/s | p50 ms | p95 ms | p99 ms | Previous p95 delta | Delivery delta (pp) |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| unreliable-only | unreliable | 99.400 | 387.958 | 0.133 | 0.196 | 0.257 | -40.62% | 0.000 |
| reliable-baseline | reliable | 100.000 | 40.406 | 0.126 | 0.162 | 0.229 | -42.71% | 0.000 |
| mixed | reliable | 100.000 | 41.028 | 0.119 | 0.172 | 0.237 | -37.08% | 0.000 |
| mixed | unreliable | 99.600 | 408.233 | 0.150 | 0.223 | 0.398 | -43.18% | -0.200 |

Reliable p95 under mixed load versus reliable-only: +6.36%.
