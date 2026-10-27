## Channel RTT / delivery

RTT is measured per request ID, including send queue delay. Missing responses are excluded from RTT and included in delivery loss.
Values are medians across runs. Throughput includes the fixed drain interval. Latency changes are informational.

| Scenario | Channel | Delivery % | Responses/s | p50 ms | p95 ms | p99 ms | Previous p95 delta | Delivery delta (pp) |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| unreliable-only | unreliable | 98.900 | 383.445 | 0.227 | 0.326 | 0.459 | +36.28% | -0.200 |
| reliable-baseline | reliable | 100.000 | 39.648 | 0.145 | 0.194 | 0.340 | +10.43% | 0.000 |
| mixed | reliable | 100.000 | 41.072 | 0.148 | 0.259 | 0.313 | +57.03% | 0.000 |
| mixed | unreliable | 99.600 | 408.662 | 0.218 | 0.346 | 0.417 | +60.96% | 0.300 |

Reliable p95 under mixed load versus reliable-only: +33.76%.
