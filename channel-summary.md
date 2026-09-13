## Channel RTT / delivery

RTT is measured per request ID, including send queue delay. Missing responses are excluded from RTT and included in delivery loss.
Values are medians across runs. Throughput includes the fixed drain interval. Latency changes are informational.

| Scenario | Channel | Delivery % | Responses/s | p50 ms | p95 ms | p99 ms | Previous p95 delta | Delivery delta (pp) |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| unreliable-only | unreliable | 100.000 | 393.277 | 0.178 | 0.303 | 0.373 | N/A | N/A |
| reliable-baseline | reliable | 100.000 | 40.011 | 0.141 | 0.192 | 0.228 | N/A | N/A |
| mixed | reliable | 100.000 | 39.608 | 0.144 | 0.250 | 0.366 | N/A | N/A |
| mixed | unreliable | 99.700 | 395.089 | 0.207 | 0.337 | 0.415 | N/A | N/A |

Reliable p95 under mixed load versus reliable-only: +30.76%.
