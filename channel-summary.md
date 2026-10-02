## Channel RTT / delivery

RTT is measured per request ID, including send queue delay. Missing responses are excluded from RTT and included in delivery loss.
Values are medians across runs. Throughput includes the fixed drain interval. Latency changes are informational.

| Scenario | Channel | Delivery % | Responses/s | p50 ms | p95 ms | p99 ms | Previous p95 delta | Delivery delta (pp) |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| unreliable-only | unreliable | 99.800 | 389.648 | 0.194 | 0.316 | 0.393 | +77.90% | 0.800 |
| reliable-baseline | reliable | 100.000 | 39.769 | 0.138 | 0.193 | 0.261 | +25.75% | 0.000 |
| mixed | reliable | 100.000 | 39.991 | 0.140 | 0.224 | 0.255 | +53.32% | 0.000 |
| mixed | unreliable | 99.800 | 399.507 | 0.208 | 0.356 | 0.438 | +98.55% | 0.100 |

Reliable p95 under mixed load versus reliable-only: +16.28%.
