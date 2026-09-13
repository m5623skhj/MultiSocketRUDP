## Channel RTT / delivery

RTT is measured per request ID, including send queue delay. Missing responses are excluded from RTT and included in delivery loss.
Values are medians across runs. Throughput includes the fixed drain interval. Latency changes are informational.

| Scenario | Channel | Delivery % | Responses/s | p50 ms | p95 ms | p99 ms | Previous p95 delta | Delivery delta (pp) |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| unreliable-only | unreliable | 99.600 | 386.561 | 0.206 | 0.338 | 0.392 | +11.45% | -0.400 |
| reliable-baseline | reliable | 100.000 | 40.969 | 0.175 | 0.269 | 0.295 | +40.42% | 0.000 |
| mixed | reliable | 100.000 | 39.822 | 0.166 | 0.251 | 0.278 | +0.32% | 0.000 |
| mixed | unreliable | 99.400 | 394.634 | 0.230 | 0.389 | 0.527 | +15.53% | -0.300 |

Reliable p95 under mixed load versus reliable-only: -6.58%.
