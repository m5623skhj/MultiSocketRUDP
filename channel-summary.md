## Channel RTT / delivery

RTT is measured per request ID, including send queue delay. Missing responses are excluded from RTT and included in delivery loss.
Values are medians across runs. Standalone v3 waits for each response/timeout; mixed throughput includes the drain interval. RTT includes send queue delay. Latency changes are informational.

| Scenario | Channel | Delivery % | Responses/s | p50 µs | p95 µs | p99 µs | Timeouts | Previous p95 delta | Delivery delta (pp) |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| unreliable-only | unreliable | 100.000 | 7172.294 | 135.7 | 163.6 | 186.1 | 0.000 | -2.39% | 0.000 |
| reliable-baseline | reliable | 100.000 | 5436.449 | 162.7 | 188.2 | 214.5 | 0.000 | +0.16% | 0.000 |
| mixed | reliable | 100.000 | 40.026 | 153.4 | 252.1 | 371.3 | N/A | -16.58% | 0.000 |
| mixed | unreliable | 99.500 | 397.055 | 222.8 | 384.1 | 592.9 | N/A | +4.12% | -0.100 |
