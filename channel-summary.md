## Channel RTT / delivery

RTT is measured per request ID, including send queue delay. Missing responses are excluded from RTT and included in delivery loss.
Values are medians across runs. Throughput includes the fixed drain interval. Latency changes are informational.

| Scenario | Channel | Delivery % | Responses/s | p50 ms | p95 ms | p99 ms | Previous p95 delta | Delivery delta (pp) |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| unreliable-only | unreliable | 99.400 | 387.345 | 0.214 | 0.349 | 0.464 | +71.63% | 0.000 |
| reliable-baseline | reliable | 100.000 | 39.662 | 0.189 | 0.301 | 0.340 | +65.77% | 0.000 |
| mixed | reliable | 100.000 | 39.792 | 0.193 | 0.286 | 0.377 | +69.73% | 0.000 |
| mixed | unreliable | 99.700 | 396.425 | 0.245 | 0.383 | 0.455 | +71.08% | 0.800 |

Reliable p95 under mixed load versus reliable-only: -5.05%.
