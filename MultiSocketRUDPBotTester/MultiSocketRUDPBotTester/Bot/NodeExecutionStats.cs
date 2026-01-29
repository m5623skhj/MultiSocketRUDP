using System.Collections.Concurrent;

namespace MultiSocketRUDPBotTester.Bot
{
    public class NodeExecutionStats
    {
        public string NodeName { get; set; } = "";
        public int ExecutionCount { get; private set; }
        public int SuccessCount { get; private set; }
        public int FailureCount { get; private set; }
        public long TotalExecutionTimeMs { get; private set; }
        public long MinExecutionTimeMs { get; private set; }
        public long MaxExecutionTimeMs { get; private set; }
        public DateTime LastExecutionTime { get; private set; }
        public string LastError { get; private set; } = "";

        public double AverageExecutionTimeMs =>
            ExecutionCount > 0 ? (double)TotalExecutionTimeMs / ExecutionCount : 0;

        public double SuccessRate =>
            ExecutionCount > 0 ? (double)SuccessCount / ExecutionCount * 100 : 0;

        internal void RecordExecution(long executionTimeMs, bool success, string? error)
        {
            ExecutionCount++;

            if (success)
                SuccessCount++;
            else
            {
                FailureCount++;
                if (error != null)
                    LastError = error;
            }

            TotalExecutionTimeMs += executionTimeMs;

            if (ExecutionCount == 1)
            {
                MinExecutionTimeMs = executionTimeMs;
                MaxExecutionTimeMs = executionTimeMs;
            }
            else
            {
                MinExecutionTimeMs = Math.Min(MinExecutionTimeMs, executionTimeMs);
                MaxExecutionTimeMs = Math.Max(MaxExecutionTimeMs, executionTimeMs);
            }

            LastExecutionTime = DateTime.Now;
        }
    }

    public class NodeStatsTracker
    {
        private readonly ConcurrentDictionary<string, NodeExecutionStats> stats = new();

        public void RecordExecution(string nodeName, long executionTimeMs, bool success, string? error = null)
        {
            var stat = stats.GetOrAdd(nodeName, _ => new NodeExecutionStats { NodeName = nodeName });
            stat.RecordExecution(executionTimeMs, success, error);
        }

        public NodeExecutionStats? GetStats(string nodeName)
        {
            return stats.GetValueOrDefault(nodeName);
        }

        public List<NodeExecutionStats> GetAllStats()
        {
            return stats.Values.OrderByDescending(s => s.ExecutionCount).ToList();
        }

        public void Reset()
        {
            stats.Clear();
        }

        public void ResetNode(string nodeName)
        {
            stats.TryRemove(nodeName, out _);
        }
    }
}