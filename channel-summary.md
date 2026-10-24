## Channel RTT / delivery

RTT is measured per request ID, including send queue delay. Missing responses are excluded from RTT and included in delivery loss.
Values are medians across runs. Throughput includes the fixed drain interval. Latency changes are informational.

| Scenario | Channel | Delivery % | Responses/s | p50 ms | p95 ms | p99 ms | Previous p95 delta | Delivery delta (pp) |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| unreliable-only | unreliable | 99.100 | 394.183 | 0.136 | 0.239 | 0.379 | -1.96% | 0.200 |
| reliable-baseline | reliable | 100.000 | 39.183 | 0.126 | 0.175 | 0.289 | -12.74% | 0.000 |
| mixed | reliable | 100.000 | 40.855 | 0.112 | 0.165 | 0.210 | -9.14% | 0.000 |
| mixed | unreliable | 99.300 | 406.099 | 0.146 | 0.215 | 0.322 | -9.06% | 1.200 |

Reliable p95 under mixed load versus reliable-only: -5.93%.
