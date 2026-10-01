## Channel RTT / delivery

RTT is measured per request ID, including send queue delay. Missing responses are excluded from RTT and included in delivery loss.
Values are medians across runs. Throughput includes the fixed drain interval. Latency changes are informational.

| Scenario | Channel | Delivery % | Responses/s | p50 ms | p95 ms | p99 ms | Previous p95 delta | Delivery delta (pp) |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| unreliable-only | unreliable | 99.000 | 387.851 | 0.123 | 0.178 | 0.246 | -29.53% | -0.600 |
| reliable-baseline | reliable | 100.000 | 39.198 | 0.125 | 0.153 | 0.198 | -14.35% | 0.000 |
| mixed | reliable | 100.000 | 40.495 | 0.109 | 0.146 | 0.187 | -33.10% | 0.000 |
| mixed | unreliable | 99.700 | 403.730 | 0.133 | 0.179 | 0.230 | -48.49% | -0.200 |

Reliable p95 under mixed load versus reliable-only: -4.63%.
