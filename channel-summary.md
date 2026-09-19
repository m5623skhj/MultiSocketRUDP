## Channel RTT / delivery

RTT is measured per request ID, including send queue delay. Missing responses are excluded from RTT and included in delivery loss.
Values are medians across runs. Throughput includes the fixed drain interval. Latency changes are informational.

| Scenario | Channel | Delivery % | Responses/s | p50 ms | p95 ms | p99 ms | Previous p95 delta | Delivery delta (pp) |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| unreliable-only | unreliable | 99.700 | 391.603 | 0.200 | 0.316 | 0.392 | +0.06% | 0.200 |
| reliable-baseline | reliable | 100.000 | 39.716 | 0.185 | 0.248 | 0.294 | +38.64% | 0.000 |
| mixed | reliable | 100.000 | 39.932 | 0.174 | 0.268 | 0.300 | +23.96% | 0.000 |
| mixed | unreliable | 100.000 | 399.317 | 0.237 | 0.389 | 0.445 | +2.91% | 0.300 |

Reliable p95 under mixed load versus reliable-only: +7.73%.
