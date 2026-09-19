## Channel RTT / delivery

RTT is measured per request ID, including send queue delay. Missing responses are excluded from RTT and included in delivery loss.
Values are medians across runs. Throughput includes the fixed drain interval. Latency changes are informational.

| Scenario | Channel | Delivery % | Responses/s | p50 ms | p95 ms | p99 ms | Previous p95 delta | Delivery delta (pp) |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| unreliable-only | unreliable | 99.500 | 392.196 | 0.189 | 0.316 | 0.363 | +51.73% | 0.200 |
| reliable-baseline | reliable | 100.000 | 40.815 | 0.143 | 0.179 | 0.206 | +12.71% | 0.000 |
| mixed | reliable | 100.000 | 40.533 | 0.141 | 0.216 | 0.284 | +52.29% | 0.000 |
| mixed | unreliable | 99.700 | 405.328 | 0.210 | 0.378 | 0.473 | +104.27% | 0.200 |

Reliable p95 under mixed load versus reliable-only: +20.49%.
