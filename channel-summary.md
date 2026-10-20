## Channel RTT / delivery

RTT is measured per request ID, including send queue delay. Missing responses are excluded from RTT and included in delivery loss.
Values are medians across runs. Throughput includes the fixed drain interval. Latency changes are informational.

| Scenario | Channel | Delivery % | Responses/s | p50 ms | p95 ms | p99 ms | Previous p95 delta | Delivery delta (pp) |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| unreliable-only | unreliable | 99.700 | 395.112 | 0.202 | 0.318 | 0.432 | +2.12% | 0.000 |
| reliable-baseline | reliable | 100.000 | 40.636 | 0.171 | 0.238 | 0.268 | +22.74% | 0.000 |
| mixed | reliable | 100.000 | 40.197 | 0.160 | 0.259 | 0.330 | +4.74% | 0.000 |
| mixed | unreliable | 99.800 | 401.570 | 0.222 | 0.361 | 0.423 | -3.88% | 0.100 |

Reliable p95 under mixed load versus reliable-only: +8.70%.
