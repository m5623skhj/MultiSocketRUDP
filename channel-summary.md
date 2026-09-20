## Channel RTT / delivery

RTT is measured per request ID, including send queue delay. Missing responses are excluded from RTT and included in delivery loss.
Values are medians across runs. Throughput includes the fixed drain interval. Latency changes are informational.

| Scenario | Channel | Delivery % | Responses/s | p50 ms | p95 ms | p99 ms | Previous p95 delta | Delivery delta (pp) |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| unreliable-only | unreliable | 99.700 | 395.872 | 0.109 | 0.186 | 0.297 | -41.25% | 0.000 |
| reliable-baseline | reliable | 100.000 | 39.724 | 0.103 | 0.167 | 0.190 | -32.90% | 0.000 |
| mixed | reliable | 100.000 | 39.956 | 0.090 | 0.153 | 0.179 | -42.62% | 0.000 |
| mixed | unreliable | 99.900 | 399.156 | 0.118 | 0.189 | 0.233 | -51.51% | -0.100 |

Reliable p95 under mixed load versus reliable-only: -7.86%.
