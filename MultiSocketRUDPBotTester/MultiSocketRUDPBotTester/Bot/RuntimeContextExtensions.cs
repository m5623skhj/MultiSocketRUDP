using System.Collections.Concurrent;

namespace MultiSocketRUDPBotTester.Bot
{
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
            ConcurrentBag<double> bag;

            if (ctx.TryGet<ConcurrentBag<double>>(key, out var existing) && existing != null)
            {
                bag = existing;
            }
            else
            {
                bag = new ConcurrentBag<double>();
                ctx.Set(key, bag);
            }

            bag.Add(value);
        }

        public static double GetAverageMetric(this RuntimeContext ctx, string name)
        {
            if (ctx.TryGet<ConcurrentBag<double>>($"__metric_{name}", out var list) && list != null && list.Count > 0)
            {
                return list.Average();
            }

            return 0;
        }

        public static double GetMinMetric(this RuntimeContext ctx, string name)
        {
            if (ctx.TryGet<ConcurrentBag<double>>($"__metric_{name}", out var list) && list != null && list.Count > 0)
            {
                return list.Min();
            }

            return 0;
        }

        public static double GetMaxMetric(this RuntimeContext ctx, string name)
        {
            if (ctx.TryGet<ConcurrentBag<double>>($"__metric_{name}", out var list) && list != null && list.Count > 0)
            {
                return list.Max();
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
    }
}