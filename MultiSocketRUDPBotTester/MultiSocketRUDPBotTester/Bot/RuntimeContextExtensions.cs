namespace MultiSocketRUDPBotTester.Bot
{
    internal sealed class MetricAggregate
    {
        private readonly object syncRoot = new();
        private long count;
        private double sum;
        private double min = double.PositiveInfinity;
        private double max = double.NegativeInfinity;

        public void Add(double value)
        {
            lock (syncRoot)
            {
                ++count;
                sum += value;
                if (value < min) min = value;
                if (value > max) max = value;
            }
        }

        public long Count
        {
            get { lock (syncRoot) { return count; } }
        }

        public double Average
        {
            get
            {
                lock (syncRoot)
                {
                    return count == 0 ? 0d : sum / count;
                }
            }
        }

        public double Min
        {
            get
            {
                lock (syncRoot)
                {
                    return count == 0 ? 0d : min;
                }
            }
        }

        public double Max
        {
            get
            {
                lock (syncRoot)
                {
                    return count == 0 ? 0d : max;
                }
            }
        }
    }

    public static class RuntimeContextExtensions
    {
        public static bool TryGet<T>(this RuntimeContext ctx, string key, out T? value) where T : notnull
        {
            if (ctx.Has(key))
            {
                try
                {
                    value = ctx.Get<T>(key);
                    return true;
                }
                catch
                {
                    value = default;
                    return false;
                }
            }
            value = default;
            return false;
        }

        public static void SetFlag(this RuntimeContext ctx, string name) => ctx.Set(name, true);
        public static void ClearFlag(this RuntimeContext ctx, string name) => ctx.Set(name, false);
        public static bool IsFlagSet(this RuntimeContext ctx, string name) => ctx.GetOrDefault(name, false);

        public static void ResetCounter(this RuntimeContext ctx, string name) => ctx.Set(name, 0);
        public static int GetCounter(this RuntimeContext ctx, string name) => ctx.GetOrDefault(name, 0);

        public static int Increment(this RuntimeContext ctx, string key, int delta = 1)
        {
            return ctx.AtomicIncrement(key, delta);
        }

        public static void StartTimer(this RuntimeContext ctx, string name)
        {
            ctx.Set($"__timer_{name}", DateTime.Now);
        }

        public static TimeSpan? GetElapsed(this RuntimeContext ctx, string name)
        {
            if (ctx.TryGet<DateTime>($"__timer_{name}", out var start))
            {
                return DateTime.Now - start;
            }

            return null;
        }

        public static void StopTimer(this RuntimeContext ctx, string name)
        {
            ctx.Set($"__timer_{name}", DateTime.MinValue);
        }

        public static void RecordMetric(this RuntimeContext ctx, string name, double value)
        {
            var key = $"__metric_{name}";
            var agg = ctx.GetOrcreate(key, () => new MetricAggregate());
            agg.Add(value);
        }

        public static double GetAverageMetric(this RuntimeContext ctx, string name)
        {
            if (ctx.TryGet<MetricAggregate>($"__metric_{name}", out var agg) && agg != null && agg.Count > 0)
            {
                return agg.Average;
            }

            return 0;
        }

        public static double GetMinMetric(this RuntimeContext ctx, string name)
        {
            if (ctx.TryGet<MetricAggregate>($"__metric_{name}", out var agg) && agg != null && agg.Count > 0)
            {
                return agg.Min;
            }

            return 0;
        }

        public static double GetMaxMetric(this RuntimeContext ctx, string name)
        {
            if (ctx.TryGet<MetricAggregate>($"__metric_{name}", out var agg) && agg != null && agg.Count > 0)
            {
                return agg.Max;
            }

            return 0;
        }

        public static void IncrementExecutionCount(this RuntimeContext ctx, string nodeName)
        {
            ctx.Increment($"__exec_{nodeName}");
        }

        public static int GetExecutionCount(this RuntimeContext ctx, string nodeName)
        {
            return ctx.GetOrDefault($"__exec_{nodeName}", 0);
        }

        internal static void RecordMetricRaw(this RuntimeContext ctx, string fullKey, double value)
        {
            
            var agg = ctx.GetOrcreate(fullKey, () => new MetricAggregate());
            agg.Add(value);
        }
    }
}
