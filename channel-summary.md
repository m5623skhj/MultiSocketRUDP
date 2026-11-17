## Channel RTT / delivery

RTT is measured per request ID, including send queue delay. Missing responses are excluded from RTT and included in delivery loss.
Values are medians across runs. Standalone v3 waits for each response/timeout; mixed throughput includes the drain interval. RTT includes send queue delay. Latency changes are informational.

| Scenario | Channel | Delivery % | Responses/s | p50 µs | p95 µs | p99 µs | Timeouts | Previous p95 delta | Delivery delta (pp) |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| unreliable-only | unreliable | 100.000 | 6337.160 | 142.5 | 168.2 | 193.1 | 0.000 | +79.89% | 0.000 |
| reliable-baseline | reliable | 100.000 | 5526.999 | 165.9 | 187.0 | 212.9 | 0.000 | +74.93% | 0.000 |
| mixed | reliable | 100.000 | 40.392 | 163.5 | 254.7 | 405.1 | N/A | +49.56% | 0.000 |
| mixed | unreliable | 99.700 | 402.587 | 227.7 | 381.3 | 559.2 | N/A | +82.97% | 0.300 |
