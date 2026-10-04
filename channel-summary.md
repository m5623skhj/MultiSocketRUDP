## Channel RTT / delivery

RTT is measured per request ID, including send queue delay. Missing responses are excluded from RTT and included in delivery loss.
Values are medians across runs. Throughput includes the fixed drain interval. Latency changes are informational.

| Scenario | Channel | Delivery % | Responses/s | p50 ms | p95 ms | p99 ms | Previous p95 delta | Delivery delta (pp) |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| unreliable-only | unreliable | 99.900 | 392.892 | 0.203 | 0.332 | 0.430 | +5.00% | 0.100 |
| reliable-baseline | reliable | 100.000 | 39.658 | 0.165 | 0.226 | 0.236 | +17.26% | 0.000 |
| mixed | reliable | 100.000 | 39.905 | 0.149 | 0.254 | 0.322 | +13.20% | 0.000 |
| mixed | unreliable | 99.500 | 397.054 | 0.220 | 0.376 | 0.458 | +5.53% | -0.300 |

Reliable p95 under mixed load versus reliable-only: +12.25%.
