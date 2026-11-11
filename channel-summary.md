## Channel RTT / delivery

RTT is measured per request ID, including send queue delay. Missing responses are excluded from RTT and included in delivery loss.
Values are medians across runs. Standalone v3 waits for each response/timeout; mixed throughput includes the drain interval. RTT includes send queue delay. Latency changes are informational.

| Scenario | Channel | Delivery % | Responses/s | p50 µs | p95 µs | p99 µs | Timeouts | Previous p95 delta | Delivery delta (pp) |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| unreliable-only | unreliable | 100.000 | 7185.064 | 133.4 | 163.0 | 182.2 | 0.000 | -6.48% | 0.000 |
| reliable-baseline | reliable | 100.000 | 5314.920 | 162.0 | 186.7 | 219.7 | 0.000 | -10.24% | 0.000 |
| mixed | reliable | 100.000 | 40.122 | 150.2 | 255.1 | 350.4 | N/A | -12.28% | 0.000 |
| mixed | unreliable | 99.300 | 398.410 | 216.8 | 380.1 | 545.8 | N/A | +0.32% | -0.300 |
